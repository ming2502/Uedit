#include "search/regex_search.h"

#include "search/regex_engine.h"

namespace uedit {

SearchOutcome RegexSearch(const SearchRequest& request,
                          std::string_view pattern_utf8,
                          const SearchOptions& options, size_t max_hits,
                          CancelToken* cancel,
                          std::atomic<uint64_t>* scanned_progress,
                          bool literal_pattern) {
  if (request.encoding == TextEncoding::Utf16LE ||
      request.encoding == TextEncoding::Utf16BE) {
    return RunRegex16(request, pattern_utf8, options, max_hits, cancel,
                      scanned_progress, literal_pattern);
  }
  return RunRegex8(request, pattern_utf8, options, max_hits, cancel,
                   scanned_progress, literal_pattern);
}

}  // namespace uedit
