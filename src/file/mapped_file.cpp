#include "file/mapped_file.h"

namespace uedit {
namespace {

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

}  // namespace

std::unique_ptr<MappedFile> MappedFile::Open(const std::wstring& path,
                                             std::wstring* error) {
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error) *error = L"无法打开文件：" + LastErrorText(GetLastError());
    return nullptr;
  }

  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) {
    if (error) *error = L"无法读取文件大小：" + LastErrorText(GetLastError());
    CloseHandle(file);
    return nullptr;
  }

  std::unique_ptr<MappedFile> mapped(new MappedFile());
  mapped->file_ = file;
  mapped->size_ = static_cast<uint64_t>(file_size.QuadPart);
  mapped->path_ = path;

  if (mapped->size_ == 0) return mapped;

  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) {
    if (error) *error = L"无法创建文件映射：" + LastErrorText(GetLastError());
    return nullptr;
  }
  mapped->mapping_ = mapping;

  void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (base == nullptr) {
    if (error) *error = L"无法映射文件视图：" + LastErrorText(GetLastError());
    return nullptr;
  }
  mapped->data_ = static_cast<const uint8_t*>(base);
  return mapped;
}

MappedFile::~MappedFile() { Close(); }

void MappedFile::Close() {
  if (data_ != nullptr) {
    UnmapViewOfFile(data_);
    data_ = nullptr;
  }
  if (mapping_ != nullptr) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
  }
  if (file_ != INVALID_HANDLE_VALUE) {
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
  }
  size_ = 0;
}

}  // namespace uedit
