#include "search/search.h"

#include "search/literal_search.h"
#include "search/regex_search.h"

namespace uedit {

SearchOutcome SearchBuffer(const SearchRequest& request,
                           std::string_view needle_utf8,
                           const SearchOptions& options, size_t max_hits,
                           CancelToken* cancel,
                           std::atomic<uint64_t>* scanned_progress) {
  if (request.data == nullptr || request.size == 0) return {};
  if (needle_utf8.empty() && !options.regex) return {};

  if (options.regex) {
    return RegexSearch(request, needle_utf8, options, max_hits, cancel,
                       scanned_progress, false);
  }
  if (request.encoding == TextEncoding::Utf16LE ||
      request.encoding == TextEncoding::Utf16BE) {
    return RegexSearch(request, needle_utf8, options, max_hits, cancel,
                       scanned_progress, true);
  }
  return LiteralSearch(request.data, request.size, needle_utf8, options,
                       max_hits, cancel, scanned_progress);
}

}  // namespace uedit
