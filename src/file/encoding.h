#pragma once

#include <cstdint>
#include <string>

namespace uedit {

enum class TextEncoding { Utf8, Utf16LE, Utf16BE, Ansi };

struct EncodingInfo {
  TextEncoding encoding = TextEncoding::Utf8;
  unsigned codepage = 65001;
  uint32_t bom_size = 0;

  int unit_size() const {
    return (encoding == TextEncoding::Utf16LE ||
            encoding == TextEncoding::Utf16BE)
               ? 2
               : 1;
  }
  bool big_endian() const { return encoding == TextEncoding::Utf16BE; }
};

EncodingInfo DetectEncoding(const uint8_t* data, uint64_t size);
std::wstring DecodeToWide(const uint8_t* data, size_t length,
                          const EncodingInfo& info);
std::wstring EncodingName(const EncodingInfo& info);

}  // namespace uedit
