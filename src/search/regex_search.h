#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "search/search.h"

namespace uedit {

// Regex search over UTF-8/ANSI (8-bit engine) or UTF-16 (16-bit engine).
// `literal_pattern` escapes the pattern so UTF-16 literal search can reuse the
// 16-bit PCRE2 engine.
SearchOutcome RegexSearch(const SearchRequest& request,
                          std::string_view pattern_utf8,
                          const SearchOptions& options, size_t max_hits,
                          CancelToken* cancel,
                          std::atomic<uint64_t>* scanned_progress,
                          bool literal_pattern);

}  // namespace uedit
