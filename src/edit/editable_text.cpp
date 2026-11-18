#include "edit/editable_text.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace uedit {
namespace {

uint64_t FindSeparator(const uint8_t* data, uint64_t size, uint64_t pos,
                       int unit_size, bool big_endian) {
  constexpr uint64_t kInvalid = ~0ull;
  if (unit_size == 1) {
    const void* hit = std::memchr(data + pos, '\n',
                                  static_cast<size_t>(size - pos));
    if (hit == nullptr) return kInvalid;
    return static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data);
  }
  uint64_t cursor = pos & ~1ull;
  while (cursor + 1 < size) {
    const void* hit = std::memchr(data + cursor, 0x0A,
                                  static_cast<size_t>(size - cursor));
    if (hit == nullptr) return kInvalid;
    const uint64_t offset =
        static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data);
    if (big_endian) {
      if ((offset & 1) == 1 && offset > 0 && data[offset - 1] == 0x00) {
        return offset - 1;
      }
    } else {
      if ((offset & 1) == 0 && offset + 1 < size && data[offset + 1] == 0x00) {
        return offset;
      }
    }
    cursor = offset + 1;
  }
  return kInvalid;
}

bool IsNewlineUnit(const uint8_t* data, uint64_t offset, bool big_endian) {
  if (big_endian) {
    return data[offset] == 0x00 && data[offset + 1] == 0x0A;
  }
  return data[offset] == 0x0A && data[offset + 1] == 0x00;
}

bool IsCarriageReturnUnit(const uint8_t* data, uint64_t offset,
                          bool big_endian) {
  if (big_endian) {
    return data[offset] == 0x00 && data[offset + 1] == 0x0D;
  }
  return data[offset] == 0x0D && data[offset + 1] == 0x00;
}

size_t Utf8SequenceLength(uint8_t lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

uint32_t DecodeUtf8Codepoint(const uint8_t* bytes, size_t length) {
  if (length == 1) return bytes[0];
  uint32_t value = bytes[0] & (0xFF >> (length + 1));
  for (size_t i = 1; i < length; ++i) {
    value = (value << 6) | (bytes[i] & 0x3F);
  }
  return value;
}

}  // namespace

bool EditableText::Load(const uint8_t* data, uint64_t size,
                        const EncodingInfo& encoding) {
  encoding_ = encoding;
  bom_ = std::min<uint64_t>(encoding.bom_size, size);
  data_.assign(data, data + size);
  Rebuild();
  return true;
}

void EditableText::Rebuild() {
  starts_.clear();
  starts_.push_back(bom_);
  const int unit_size = encoding_.unit_size();
  const bool big_endian = encoding_.big_endian();
  uint64_t pos = bom_;
  while (pos < data_.size()) {
    const uint64_t separator =
        FindSeparator(data_.data(), data_.size(), pos, unit_size, big_endian);
    if (separator == ~0ull) break;
    pos = separator + static_cast<uint64_t>(unit_size);
    starts_.push_back(pos);
  }
}

uint64_t EditableText::line_start(uint64_t line) const {
  if (line <= 1 || starts_.empty()) return bom_;
  if (line > starts_.size()) return data_.size();
  return starts_[line - 1];
}

uint64_t EditableText::line_end(uint64_t line) const {
  if (starts_.empty()) return data_.size();
  if (line + 1 > starts_.size()) return data_.size();
  return starts_[line];
}

uint64_t EditableText::content_end(uint64_t line) const {
  uint64_t begin = line_start(line);
  uint64_t end = line_end(line);
  if (encoding_.unit_size() == 1) {
    while (end > begin &&
           (data_[end - 1] == '\n' || data_[end - 1] == '\r')) {
      --end;
    }
    return end;
  }
  while (end >= begin + 2) {
    if (IsNewlineUnit(data_.data(), end - 2, encoding_.big_endian()) ||
        IsCarriageReturnUnit(data_.data(), end - 2,
                             encoding_.big_endian())) {
      end -= 2;
    } else {
      break;
    }
  }
  return end;
}

uint64_t EditableText::offset_to_line(uint64_t offset) const {
  if (starts_.empty()) return 1;
  if (offset > data_.size()) offset = data_.size();
  auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
  uint64_t line = static_cast<uint64_t>(it - starts_.begin());
  if (line < 1) line = 1;
  if (line > starts_.size()) line = starts_.size();
  return line;
}

bool EditableText::next_line_start(uint64_t pos, uint64_t max_scan,
                                   uint64_t* next) const {
  if (next == nullptr || pos >= data_.size()) return false;
  const uint64_t limit = std::min<uint64_t>(data_.size(), pos + max_scan);
  const uint64_t separator = FindSeparator(
      data_.data(), limit, pos, encoding_.unit_size(),
      encoding_.big_endian());
  if (separator == ~0ull) return false;
  const uint64_t value = separator + encoding_.unit_size();
  if (value > data_.size()) return false;
  *next = value;
  return true;
}

void EditableText::Insert(uint64_t offset, const std::string& bytes) {
  if (offset > data_.size()) offset = data_.size();
  if (bytes.empty()) return;
  data_.insert(data_.begin() + static_cast<ptrdiff_t>(offset), bytes.begin(),
               bytes.end());
  Rebuild();
}

void EditableText::Erase(uint64_t begin, uint64_t end) {
  begin = std::min(begin, data_.size());
  end = std::min(std::max(end, begin), data_.size());
  if (begin >= end) return;
  data_.erase(data_.begin() + static_cast<ptrdiff_t>(begin),
              data_.begin() + static_cast<ptrdiff_t>(end));
  Rebuild();
}

void EditableText::Replace(uint64_t begin, uint64_t end,
                           const std::string& bytes) {
  begin = std::min(begin, data_.size());
  end = std::min(std::max(end, begin), data_.size());
  if (begin == end && bytes.empty()) return;
  data_.erase(data_.begin() + static_cast<ptrdiff_t>(begin),
              data_.begin() + static_cast<ptrdiff_t>(end));
  data_.insert(data_.begin() + static_cast<ptrdiff_t>(begin), bytes.begin(),
               bytes.end());
  Rebuild();
}

std::string EditableText::Slice(uint64_t begin, uint64_t end) const {
  begin = std::min(begin, data_.size());
  end = std::min(std::max(end, begin), data_.size());
  return std::string(reinterpret_cast<const char*>(data_.data() + begin),
                     static_cast<size_t>(end - begin));
}

uint64_t EditableText::byte_offset_for_char(uint64_t line,
                                            uint32_t char_index) const {
  const uint64_t begin = line_start(line);
  const uint64_t end = content_end(line);
  if (encoding_.encoding == TextEncoding::Utf16LE ||
      encoding_.encoding == TextEncoding::Utf16BE) {
    const uint64_t units = (end - begin) / 2;
    if (char_index >= units) return end;
    return begin + static_cast<uint64_t>(char_index) * 2;
  }
  if (encoding_.encoding == TextEncoding::Utf8) {
    uint32_t units = 0;
    uint64_t offset = begin;
    while (offset < end) {
      if (units >= char_index) return offset;
      const size_t length = Utf8SequenceLength(data_[offset]);
      const size_t available =
          static_cast<size_t>(std::min<uint64_t>(end - offset, length));
      const uint32_t codepoint =
          DecodeUtf8Codepoint(data_.data() + offset, available);
      const uint32_t added = codepoint > 0xFFFF ? 2u : 1u;
      units += added;
      offset += available;
    }
    return end;
  }
  // ANSI / DBCS.
  uint32_t units = 0;
  uint64_t offset = begin;
  while (offset < end) {
    if (units >= char_index) return offset;
    uint8_t byte = data_[offset];
    const bool lead = IsDBCSLeadByteEx(encoding_.codepage, byte) != FALSE;
    offset += (lead && offset + 1 < end) ? 2 : 1;
    ++units;
  }
  return end;
}

uint32_t EditableText::char_index_for_byte(uint64_t line,
                                           uint64_t offset) const {
  const uint64_t begin = line_start(line);
  const uint64_t end = content_end(line);
  offset = std::min(std::max(offset, begin), end);
  if (encoding_.encoding == TextEncoding::Utf16LE ||
      encoding_.encoding == TextEncoding::Utf16BE) {
    return static_cast<uint32_t>((offset - begin) / 2);
  }
  if (encoding_.encoding == TextEncoding::Utf8) {
    uint32_t units = 0;
    uint64_t cursor = begin;
    while (cursor < offset) {
      const size_t length = Utf8SequenceLength(data_[cursor]);
      const size_t available =
          static_cast<size_t>(std::min<uint64_t>(end - cursor, length));
      const uint32_t codepoint =
          DecodeUtf8Codepoint(data_.data() + cursor, available);
      units += codepoint > 0xFFFF ? 2u : 1u;
      cursor += available;
    }
    return units;
  }
  uint32_t units = 0;
  uint64_t cursor = begin;
  while (cursor < offset) {
    const uint8_t byte = data_[cursor];
    const bool lead = IsDBCSLeadByteEx(encoding_.codepage, byte) != FALSE;
    cursor += (lead && cursor + 1 < end) ? 2 : 1;
    ++units;
  }
  return units;
}

std::wstring EditableText::DecodeRange(uint64_t begin, uint64_t end) const {
  begin = std::min(begin, data_.size());
  end = std::min(std::max(end, begin), data_.size());
  return DecodeToWide(data_.data() + begin, static_cast<size_t>(end - begin),
                      encoding_);
}

std::string EditableText::EncodeWide(const std::wstring& text) const {
  if (text.empty()) return {};
  if (encoding_.encoding == TextEncoding::Utf8) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                     static_cast<int>(text.size()), nullptr, 0,
                                     nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), &out[0], length,
                        nullptr, nullptr);
    return out;
  }
  if (encoding_.encoding == TextEncoding::Ansi) {
    int length = WideCharToMultiByte(encoding_.codepage, 0, text.c_str(),
                                     static_cast<int>(text.size()), nullptr, 0,
                                     nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(encoding_.codepage, 0, text.c_str(),
                        static_cast<int>(text.size()), &out[0], length,
                        nullptr, nullptr);
    return out;
  }
  std::string out(text.size() * 2, '\0');
  const bool big_endian = encoding_.big_endian();
  for (size_t i = 0; i < text.size(); ++i) {
    const uint16_t unit = static_cast<uint16_t>(text[i]);
    if (big_endian) {
      out[i * 2] = static_cast<char>((unit >> 8) & 0xFF);
      out[i * 2 + 1] = static_cast<char>(unit & 0xFF);
    } else {
      out[i * 2] = static_cast<char>(unit & 0xFF);
      out[i * 2 + 1] = static_cast<char>((unit >> 8) & 0xFF);
    }
  }
  return out;
}

}  // namespace uedit
