#include "file/encoding.h"

#include <windows.h>

#include <algorithm>

namespace uedit {
namespace {

bool ValidateUtf8Sample(const uint8_t* data, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = data[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    // A sequence cut by the end of the sample is not evidence against UTF-8.
    if (i + static_cast<size_t>(extra) >= length) return true;
    for (int k = 1; k <= extra; ++k) {
      uint8_t cc = data[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (extra == 1 && cp < 0x80) return false;
    if (extra == 2 && cp < 0x800) return false;
    if (extra == 3 && cp < 0x10000) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += static_cast<size_t>(extra) + 1;
  }
  return true;
}

}  // namespace

EncodingInfo DetectEncoding(const uint8_t* data, uint64_t size) {
  EncodingInfo info;
  if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
    info.encoding = TextEncoding::Utf8;
    info.codepage = 65001;
    info.bom_size = 3;
    return info;
  }
  if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
    info.encoding = TextEncoding::Utf16LE;
    info.codepage = 1200;
    info.bom_size = 2;
    return info;
  }
  if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
    info.encoding = TextEncoding::Utf16BE;
    info.codepage = 1201;
    info.bom_size = 2;
    return info;
  }
  size_t sample = static_cast<size_t>(std::min<uint64_t>(size, 1u << 20));
  if (sample == 0 || ValidateUtf8Sample(data, sample)) {
    info.encoding = TextEncoding::Utf8;
    info.codepage = 65001;
    return info;
  }
  info.encoding = TextEncoding::Ansi;
  info.codepage = GetACP();
  return info;
}

std::wstring DecodeToWide(const uint8_t* data, size_t length,
                          const EncodingInfo& info) {
  if (length == 0) return {};
  switch (info.encoding) {
    case TextEncoding::Utf8: {
      int wide_length = MultiByteToWideChar(
          CP_UTF8, 0, reinterpret_cast<const char*>(data),
          static_cast<int>(length), nullptr, 0);
      if (wide_length <= 0) return {};
      std::wstring out(static_cast<size_t>(wide_length), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data),
                          static_cast<int>(length), &out[0], wide_length);
      return out;
    }
    case TextEncoding::Utf16LE: {
      size_t chars = length / 2;
      if (chars == 0) return {};
      return std::wstring(reinterpret_cast<const wchar_t*>(data), chars);
    }
    case TextEncoding::Utf16BE: {
      size_t chars = length / 2;
      std::wstring out(chars, L'\0');
      for (size_t i = 0; i < chars; ++i) {
        out[i] = static_cast<wchar_t>((data[i * 2] << 8) | data[i * 2 + 1]);
      }
      return out;
    }
    case TextEncoding::Ansi: {
      int wide_length = MultiByteToWideChar(
          info.codepage, 0, reinterpret_cast<const char*>(data),
          static_cast<int>(length), nullptr, 0);
      if (wide_length <= 0) return {};
      std::wstring out(static_cast<size_t>(wide_length), L'\0');
      MultiByteToWideChar(info.codepage, 0,
                          reinterpret_cast<const char*>(data),
                          static_cast<int>(length), &out[0], wide_length);
      return out;
    }
  }
  return {};
}

std::wstring EncodingName(const EncodingInfo& info) {
  switch (info.encoding) {
    case TextEncoding::Utf8:
      return info.bom_size > 0 ? L"UTF-8 BOM" : L"UTF-8";
    case TextEncoding::Utf16LE:
      return L"UTF-16 LE";
    case TextEncoding::Utf16BE:
      return L"UTF-16 BE";
    case TextEncoding::Ansi:
      return L"ANSI " + std::to_wstring(info.codepage);
  }
  return L"未知";
}

}  // namespace uedit
