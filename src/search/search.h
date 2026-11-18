#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/cancel_token.h"
#include "file/encoding.h"

namespace uedit {

struct SearchHit {
  uint64_t offset = 0;
  uint32_t length = 0;
};

struct SearchOptions {
  bool case_sensitive = false;
  bool whole_word = false;
  bool regex = false;
};

struct SearchOutcome {
  std::vector<SearchHit> hits;  // sorted by offset
  uint64_t scanned_bytes = 0;
  uint64_t total_hits = 0;
  double seconds = 0.0;
  bool cancelled = false;
  bool truncated = false;
  bool pattern_error = false;
  std::string error_message;  // UTF-8
};

struct SearchRequest {
  const uint8_t* data = nullptr;
  uint64_t size = 0;
  TextEncoding encoding = TextEncoding::Utf8;
  bool big_endian = false;
  unsigned codepage = 65001;
};

// Dispatches to the byte-oriented literal matcher or the PCRE2 engine
// (8-bit for UTF-8/ANSI, 16-bit for UTF-16).
SearchOutcome SearchBuffer(const SearchRequest& request,
                           std::string_view needle_utf8,
                           const SearchOptions& options, size_t max_hits,
                           CancelToken* cancel,
                           std::atomic<uint64_t>* scanned_progress = nullptr);

}  // namespace uedit
