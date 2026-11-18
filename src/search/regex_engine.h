#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "search/search.h"

namespace uedit {

SearchOutcome RunRegex8(const SearchRequest& request,
                        std::string_view pattern_utf8,
                        const SearchOptions& options, size_t max_hits,
                        CancelToken* cancel,
                        std::atomic<uint64_t>* scanned_progress,
                        bool literal_pattern);

SearchOutcome RunRegex16(const SearchRequest& request,
                         std::string_view pattern_utf8,
                         const SearchOptions& options, size_t max_hits,
                         CancelToken* cancel,
                         std::atomic<uint64_t>* scanned_progress,
                         bool literal_pattern);

}  // namespace uedit
