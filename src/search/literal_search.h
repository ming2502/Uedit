#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "common/cancel_token.h"
#include "search/search.h"

namespace uedit {

// Byte-oriented (UTF-8/ANSI) literal search. Kept as a standalone entry point
// for tests and benchmarks; the UI goes through SearchBuffer.
SearchOutcome LiteralSearch(const uint8_t* data, uint64_t size,
                            std::string_view needle,
                            const SearchOptions& options, size_t max_hits,
                            CancelToken* cancel,
                            std::atomic<uint64_t>* scanned_progress = nullptr);

}  // namespace uedit
