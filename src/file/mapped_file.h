#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace uedit {

// Read-only memory-mapped file. x64 only: one view for the whole file.
class MappedFile {
 public:
  static std::unique_ptr<MappedFile> Open(const std::wstring& path, std::wstring* error);
  ~MappedFile();

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  const uint8_t* data() const { return data_; }
  uint64_t size() const { return size_; }
  const std::wstring& path() const { return path_; }

  std::string_view view(uint64_t offset, uint64_t length) const {
    if (offset >= size_) return {};
    uint64_t available = size_ - offset;
    if (length > available) length = available;
    return std::string_view(reinterpret_cast<const char*>(data_ + offset),
                            static_cast<size_t>(length));
  }

 private:
  MappedFile() = default;
  void Close();

  HANDLE file_ = INVALID_HANDLE_VALUE;
  HANDLE mapping_ = nullptr;
  const uint8_t* data_ = nullptr;
  uint64_t size_ = 0;
  std::wstring path_;
};

}  // namespace uedit
