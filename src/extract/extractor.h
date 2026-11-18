#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "common/cancel_token.h"
#include "file/encoding.h"
#include "search/search.h"

namespace uedit {

enum class ExtractMode { WholeLines = 0, MatchOnly = 1, WithContext = 2 };

struct ExtractRange {
  uint64_t begin = 0;
  uint64_t end = 0;
};

struct ExtractWriteResult {
  bool ok = false;
  bool cancelled = false;
  uint64_t bytes_written = 0;
  std::wstring error;
};

struct RewriteResult {
  bool ok = false;
  bool cancelled = false;
  uint64_t bytes_written = 0;
  std::wstring error;
};

// Sorts, expands to line boundaries (unless MatchOnly), applies context and
// merges overlapping ranges. `dedupe` hashes range contents.
std::vector<ExtractRange> BuildExtractRanges(
    const uint8_t* data, uint64_t size, const std::vector<SearchHit>& hits,
    ExtractMode mode, int context_lines, bool dedupe,
    const EncodingInfo& encoding);

// Streams the selected ranges into `output_path` (via a .part file and an
// atomic rename). Writes the source BOM when present. `newline_between_ranges`
// inserts CRLF between non-line ranges.
ExtractWriteResult WriteExtractFile(
    const uint8_t* data, uint64_t size,
    const std::vector<ExtractRange>& ranges, const EncodingInfo& encoding,
    const std::wstring& output_path, bool newline_between_ranges,
    CancelToken* cancel, std::atomic<uint64_t>* written_progress);

// Copies the source to a sibling temp file while skipping `deletions`, flushes,
// then atomically replaces the source. Optionally keeps `<source>.bak`.
RewriteResult RewriteFileWithoutRanges(
    const std::wstring& source_path, const uint8_t* data, uint64_t size,
    std::vector<ExtractRange> deletions, bool make_backup,
    CancelToken* cancel, std::atomic<uint64_t>* written_progress);

bool IsFileWritable(const std::wstring& path, std::wstring* error);

// Writes an in-memory document to `path` through a sibling temp file and
// ReplaceFileW (no backup). Used by the editable document model.
RewriteResult SaveBufferAtomically(const std::wstring& path,
                                   const uint8_t* data, uint64_t size,
                                   std::wstring* error);

}  // namespace uedit
