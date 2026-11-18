#include "extract/extractor.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace uedit {
namespace {

constexpr uint64_t kInvalidOffset = ~0ull;
constexpr size_t kWriteBuffer = 4u << 20;

std::wstring LastErrorText(DWORD code) {
  wchar_t* buffer = nullptr;
  DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring message;
  if (length > 0 && buffer != nullptr) {
    message.assign(buffer, length);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' ||
            message.back() == L' ')) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) LocalFree(buffer);
  if (message.empty()) message = L"错误码 " + std::to_wstring(code);
  return message;
}

uint64_t LineStartBefore(const uint8_t* data, uint64_t size, uint64_t position,
                         int unit_size, bool big_endian) {
  if (size == 0) return 0;
  if (position > size) position = size;
  if (unit_size == 1) {
    uint64_t cursor = position;
    while (cursor > 0) {
      --cursor;
      if (data[cursor] == '\n') return cursor + 1;
    }
    return 0;
  }
  uint64_t cursor = position & ~1ull;
  while (cursor >= 2) {
    cursor -= 2;
    const uint8_t a = data[cursor];
    const uint8_t b = data[cursor + 1];
    if (big_endian) {
      if (a == 0x00 && b == 0x0A) return cursor + 2;
    } else {
      if (a == 0x0A && b == 0x00) return cursor + 2;
    }
  }
  return 0;
}

uint64_t NextLineStart(const uint8_t* data, uint64_t size, uint64_t position,
                       int unit_size, bool big_endian) {
  if (position >= size) return size;
  if (unit_size == 1) {
    for (uint64_t i = position; i < size; ++i) {
      if (data[i] == '\n') return i + 1;
    }
    return size;
  }
  uint64_t cursor = position & ~1ull;
  while (cursor + 1 < size) {
    const uint8_t a = data[cursor];
    const uint8_t b = data[cursor + 1];
    const bool newline = big_endian ? (a == 0x00 && b == 0x0A)
                                    : (a == 0x0A && b == 0x00);
    if (newline) return cursor + 2;
    cursor += 2;
  }
  return size;
}

uint64_t HashRange(const uint8_t* data, const ExtractRange& range) {
  uint64_t hash = 1469598103934665603ull;
  for (uint64_t i = range.begin; i < range.end; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  hash ^= range.end - range.begin;
  return hash;
}

void MergeRanges(std::vector<ExtractRange>* ranges) {
  if (ranges->empty()) return;
  std::sort(ranges->begin(), ranges->end(),
            [](const ExtractRange& a, const ExtractRange& b) {
              if (a.begin != b.begin) return a.begin < b.begin;
              return a.end < b.end;
            });
  std::vector<ExtractRange> merged;
  merged.reserve(ranges->size());
  for (const ExtractRange& range : *ranges) {
    if (range.end <= range.begin) continue;
    if (!merged.empty() && range.begin <= merged.back().end) {
      merged.back().end = std::max(merged.back().end, range.end);
    } else {
      merged.push_back(range);
    }
  }
  *ranges = std::move(merged);
}

bool EndsWithNewline(const uint8_t* data, uint64_t size,
                     const ExtractRange& range, int unit_size,
                     bool big_endian) {
  (void)size;
  if (range.end <= range.begin) return false;
  if (unit_size == 1) return data[range.end - 1] == '\n';
  if (range.end < 2) return false;
  const uint8_t a = data[range.end - 2];
  const uint8_t b = data[range.end - 1];
  return big_endian ? (a == 0x00 && b == 0x0A) : (a == 0x0A && b == 0x00);
}

bool HasFreeSpace(const std::wstring& directory, uint64_t needed) {
  ULARGE_INTEGER free_bytes{};
  if (!GetDiskFreeSpaceExW(directory.c_str(), &free_bytes, nullptr, nullptr)) {
    return true;  // Do not block the operation if the query fails.
  }
  return free_bytes.QuadPart >= needed;
}

std::wstring DirectoryOf(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L".";
  return path.substr(0, slash);
}

class FileWriter {
 public:
  ~FileWriter() { Close(); }

  bool Open(const std::wstring& path, std::wstring* error) {
    handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      *error = L"无法创建文件：" + LastErrorText(GetLastError());
      return false;
    }
    buffer_.reserve(kWriteBuffer);
    return true;
  }

  bool Append(const uint8_t* data, size_t size, std::wstring* error) {
    while (size > 0) {
      const size_t space = kWriteBuffer - buffer_.size();
      const size_t take = std::min(space, size);
      buffer_.insert(buffer_.end(), data, data + take);
      data += take;
      size -= take;
      if (buffer_.size() >= kWriteBuffer && !Flush(error)) return false;
    }
    return true;
  }

  bool AppendByte(char byte, std::wstring* error) {
    return Append(reinterpret_cast<const uint8_t*>(&byte), 1, error);
  }

  bool Flush(std::wstring* error) {
    if (buffer_.empty()) return true;
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(buffer_.size());
    if (!WriteFile(handle_, buffer_.data(), size, &written, nullptr) ||
        written != size) {
      *error = L"写入失败：" + LastErrorText(GetLastError());
      buffer_.clear();
      return false;
    }
    buffer_.clear();
    return true;
  }

  bool Finish(std::wstring* error) {
    if (!Flush(error)) return false;
    if (!FlushFileBuffers(handle_)) {
      *error = L"刷新文件失败：" + LastErrorText(GetLastError());
      return false;
    }
    Close();
    return true;
  }

  void Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::vector<uint8_t> buffer_;
};

}  // namespace

std::vector<ExtractRange> BuildExtractRanges(
    const uint8_t* data, uint64_t size, const std::vector<SearchHit>& hits,
    ExtractMode mode, int context_lines, bool dedupe,
    const EncodingInfo& encoding) {
  std::vector<ExtractRange> ranges;
  if (data == nullptr || size == 0 || hits.empty()) return ranges;

  const int unit_size = encoding.unit_size();
  const bool big_endian = encoding.big_endian();
  ranges.reserve(hits.size());

  for (const SearchHit& hit : hits) {
    if (hit.offset >= size) continue;
    uint64_t begin = hit.offset;
    uint64_t end = std::min<uint64_t>(size, hit.offset + hit.length);

    if (mode != ExtractMode::MatchOnly) {
      begin = LineStartBefore(data, size, begin, unit_size, big_endian);
      uint64_t last = hit.length > 0 ? end - 1 : begin;
      if (last >= size) last = size > 0 ? size - 1 : 0;
      end = NextLineStart(data, size, last, unit_size, big_endian);
      if (mode == ExtractMode::WithContext) {
        for (int i = 0; i < context_lines && begin > 0; ++i) {
          begin = LineStartBefore(data, size, begin - 1, unit_size,
                                  big_endian);
        }
        for (int i = 0; i < context_lines && end < size; ++i) {
          end = NextLineStart(data, size, end, unit_size, big_endian);
        }
      }
    }

    if (end <= begin) continue;
    ranges.push_back(ExtractRange{begin, end});
  }

  MergeRanges(&ranges);

  if (dedupe && !ranges.empty()) {
    std::unordered_set<uint64_t> seen;
    seen.reserve(ranges.size());
    std::vector<ExtractRange> unique;
    unique.reserve(ranges.size());
    for (const ExtractRange& range : ranges) {
      const uint64_t hash = HashRange(data, range);
      if (seen.insert(hash).second) unique.push_back(range);
    }
    ranges = std::move(unique);
  }
  return ranges;
}

ExtractWriteResult WriteExtractFile(
    const uint8_t* data, uint64_t size,
    const std::vector<ExtractRange>& ranges, const EncodingInfo& encoding,
    const std::wstring& output_path, bool newline_between_ranges,
    CancelToken* cancel, std::atomic<uint64_t>* written_progress) {
  ExtractWriteResult result;
  if (data == nullptr || ranges.empty()) {
    result.error = L"没有可导出的内容";
    return result;
  }

  const std::wstring directory = DirectoryOf(output_path);
  uint64_t total_bytes = 0;
  for (size_t i = 0; i < ranges.size(); ++i) {
    total_bytes += ranges[i].end - ranges[i].begin;
    if (newline_between_ranges && i > 0) total_bytes += 2;
  }
  if (!HasFreeSpace(directory, total_bytes + (16ull << 20))) {
    result.error = L"目标磁盘空间不足";
    return result;
  }

  const std::wstring temp_path = output_path + L".part";
  DeleteFileW(temp_path.c_str());
  FileWriter writer;
  std::wstring error;
  if (!writer.Open(temp_path, &error)) {
    result.error = error;
    return result;
  }

  uint64_t written = 0;
  bool ok = true;
  const uint32_t bom_size = encoding.bom_size;
  if (bom_size > 0 && bom_size <= size) {
    if (!writer.Append(data, bom_size, &error)) {
      ok = false;
    }
    written += bom_size;
    if (written_progress != nullptr) {
      written_progress->store(written, std::memory_order_relaxed);
    }
  }

  for (size_t i = 0; ok && i < ranges.size(); ++i) {
    if (cancel != nullptr && cancel->cancelled()) {
      result.cancelled = true;
      ok = false;
      break;
    }
    const ExtractRange& range = ranges[i];
    if (newline_between_ranges && i > 0 &&
        !EndsWithNewline(data, size, ranges[i - 1], encoding.unit_size(),
                         encoding.big_endian())) {
      if (!writer.Append(reinterpret_cast<const uint8_t*>("\r\n"), 2, &error)) {
        ok = false;
        break;
      }
      written += 2;
    }
    uint64_t begin = range.begin;
    if (bom_size > 0 && begin < bom_size) begin = bom_size;
    if (begin >= range.end) continue;
    if (!writer.Append(data + begin, static_cast<size_t>(range.end - begin),
                       &error)) {
      ok = false;
      break;
    }
    written += range.end - begin;
    if (written_progress != nullptr) {
      written_progress->store(written, std::memory_order_relaxed);
    }
  }

  if (ok) ok = writer.Finish(&error);
  writer.Close();
  if (!ok) {
    DeleteFileW(temp_path.c_str());
    if (!result.cancelled) result.error = error;
    return result;
  }

  if (!MoveFileExW(temp_path.c_str(), output_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    result.error = L"无法写入目标文件：" + LastErrorText(GetLastError());
    DeleteFileW(temp_path.c_str());
    return result;
  }

  result.ok = true;
  result.bytes_written = written;
  return result;
}

bool IsFileWritable(const std::wstring& path, std::wstring* error) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    if (error) *error = L"无法读取文件属性：" + LastErrorText(GetLastError());
    return false;
  }
  if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
    if (error) *error = L"源文件为只读，无法剪切";
    return false;
  }
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (error) *error = L"源文件不可写：" + LastErrorText(GetLastError());
    return false;
  }
  CloseHandle(handle);
  return true;
}

RewriteResult SaveBufferAtomically(const std::wstring& path,
                                   const uint8_t* data, uint64_t size,
                                   std::wstring* error) {
  RewriteResult result;
  std::wstring path_error;
  if (!IsFileWritable(path, &path_error)) {
    result.error = path_error;
    if (error) *error = path_error;
    return result;
  }
  if (!HasFreeSpace(DirectoryOf(path), size + (16ull << 20))) {
    result.error = L"源文件所在磁盘空间不足";
    if (error) *error = result.error;
    return result;
  }

  const std::wstring temp_path = path + L".uedit_tmp";
  DeleteFileW(temp_path.c_str());
  FileWriter writer;
  std::wstring write_error;
  if (!writer.Open(temp_path, &write_error)) {
    result.error = write_error;
    if (error) *error = write_error;
    return result;
  }
  uint64_t written = 0;
  while (written < size) {
    const size_t take = static_cast<size_t>(
        std::min<uint64_t>(size - written, kWriteBuffer));
    if (!writer.Append(data + written, take, &write_error)) {
      writer.Close();
      DeleteFileW(temp_path.c_str());
      result.error = write_error;
      if (error) *error = write_error;
      return result;
    }
    written += take;
  }
  if (!writer.Finish(&write_error)) {
    DeleteFileW(temp_path.c_str());
    result.error = write_error;
    if (error) *error = write_error;
    return result;
  }
  if (!ReplaceFileW(path.c_str(), temp_path.c_str(), nullptr,
                    REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    result.error = L"原子替换失败：" + LastErrorText(GetLastError());
    if (error) *error = result.error;
    DeleteFileW(temp_path.c_str());
    return result;
  }
  result.ok = true;
  result.bytes_written = size;
  return result;
}

RewriteResult RewriteFileWithoutRanges(
    const std::wstring& source_path, const uint8_t* data, uint64_t size,
    std::vector<ExtractRange> deletions, bool make_backup,
    CancelToken* cancel, std::atomic<uint64_t>* written_progress) {
  RewriteResult result;
  std::wstring error;
  if (!IsFileWritable(source_path, &error)) {
    result.error = error;
    return result;
  }

  MergeRanges(&deletions);
  uint64_t removed = 0;
  for (const ExtractRange& range : deletions) {
    removed += range.end - range.begin;
  }
  const uint64_t total_bytes = size - removed;

  if (!HasFreeSpace(DirectoryOf(source_path), size + (16ull << 20))) {
    result.error = L"源文件所在磁盘空间不足";
    return result;
  }

  const std::wstring temp_path = source_path + L".uedit_tmp";
  DeleteFileW(temp_path.c_str());
  FileWriter writer;
  if (!writer.Open(temp_path, &error)) {
    result.error = error;
    return result;
  }

  uint64_t cursor = 0;
  uint64_t written = 0;
  bool ok = true;
  auto copy_span = [&](uint64_t begin, uint64_t end) {
    if (begin >= end) return true;
    const uint8_t* source = data + begin;
    uint64_t remaining = end - begin;
    while (remaining > 0) {
      if (cancel != nullptr && cancel->cancelled()) {
        result.cancelled = true;
        return false;
      }
      const size_t take =
          static_cast<size_t>(std::min<uint64_t>(remaining, kWriteBuffer));
      if (!writer.Append(source, take, &error)) return false;
      source += take;
      remaining -= take;
      written += take;
      if (written_progress != nullptr) {
        written_progress->store(written, std::memory_order_relaxed);
      }
    }
    return true;
  };

  for (const ExtractRange& range : deletions) {
    const uint64_t begin = std::min(range.begin, size);
    const uint64_t end = std::min(range.end, size);
    if (begin > cursor) {
      if (!copy_span(cursor, begin)) {
        ok = false;
        break;
      }
    }
    cursor = std::max(cursor, end);
  }
  if (ok && cursor < size) ok = copy_span(cursor, size);
  if (ok) ok = writer.Finish(&error);
  writer.Close();

  if (!ok) {
    DeleteFileW(temp_path.c_str());
    if (!result.cancelled) result.error = error;
    return result;
  }

  std::wstring backup_path;
  const wchar_t* backup = nullptr;
  if (make_backup) {
    backup_path = source_path + L".bak";
    DeleteFileW(backup_path.c_str());
    backup = backup_path.c_str();
  }
  if (!ReplaceFileW(source_path.c_str(), temp_path.c_str(), backup,
                    REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    result.error = L"原子替换失败：" + LastErrorText(GetLastError());
    DeleteFileW(temp_path.c_str());
    return result;
  }

  result.ok = true;
  result.bytes_written = total_bytes;
  return result;
}

}  // namespace uedit
