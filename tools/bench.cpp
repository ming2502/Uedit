#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "common/stopwatch.h"
#include "file/encoding.h"
#include "file/line_index.h"
#include "file/mapped_file.h"
#include "search/literal_search.h"
#include "search/search.h"

namespace {

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                      static_cast<int>(text.size()), &out[0], length, nullptr,
                      nullptr);
  return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    wprintf(L"usage: uedit_bench <file> [needle]\n");
    return 1;
  }

  const std::wstring path = argv[1];
  std::wstring needle_wide = argc >= 3 ? argv[2] : L"NEEDLE_2026";

  uedit::Stopwatch watch;
  std::wstring error;
  std::unique_ptr<uedit::MappedFile> file =
      uedit::MappedFile::Open(path, &error);
  if (!file) {
    wprintf(L"open failed: %ls\n", error.c_str());
    return 2;
  }
  uedit::EncodingInfo encoding =
      uedit::DetectEncoding(file->data(), file->size());
  const double open_ms = watch.ElapsedMs();

  wprintf(L"file:        %ls\n", path.c_str());
  wprintf(L"size:        %.1f MB\n",
          static_cast<double>(file->size()) / (1024.0 * 1024.0));
  wprintf(L"encoding:    %ls\n", uedit::EncodingName(encoding).c_str());
  wprintf(L"open (mmap): %.2f ms\n", open_ms);

  uedit::LineIndex index;
  index.Prepare(file->data(), file->size(), encoding.bom_size,
                encoding.unit_size(), encoding.big_endian());
  watch.Reset();
  index.Build(nullptr);
  const double index_ms = watch.ElapsedMs();
  const uint64_t lines = index.total_lines();
  wprintf(L"index:       %.2f ms, lines=%llu, avg=%.1f bytes/line\n", index_ms,
          lines,
          lines > 0 ? static_cast<double>(file->size()) / static_cast<double>(lines)
                    : 0.0);

  std::mt19937_64 random(1234567);
  std::uniform_int_distribution<uint64_t> distribution(1, std::max<uint64_t>(1, lines));
  const int samples = 5000;
  double total_ms = 0.0;
  double worst_ms = 0.0;
  for (int i = 0; i < samples; ++i) {
    uint64_t line = distribution(random);
    watch.Reset();
    uint64_t offset = index.line_start(line);
    double elapsed = watch.ElapsedMs();
    total_ms += elapsed;
    worst_ms = std::max(worst_ms, elapsed);
    (void)offset;
  }
  wprintf(L"goto:        avg %.3f ms, worst %.3f ms (%d samples)\n",
          total_ms / samples, worst_ms, samples);

  watch.Reset();
  std::uniform_int_distribution<uint64_t> offsets(0, std::max<uint64_t>(1, file->size() - 1));
  double resolve_total = 0.0;
  double resolve_worst = 0.0;
  for (int i = 0; i < 1000; ++i) {
    uint64_t offset = offsets(random);
    watch.Reset();
    uint64_t line = index.offset_to_line(offset);
    double elapsed = watch.ElapsedMs();
    resolve_total += elapsed;
    resolve_worst = std::max(resolve_worst, elapsed);
    (void)line;
  }
  wprintf(L"offset->line avg %.3f ms, worst %.3f ms (1000 samples)\n",
          resolve_total / 1000.0, resolve_worst);

  if (encoding.unit_size() == 1) {
    const std::string needle = WideToUtf8(needle_wide);
    uedit::SearchOptions options;
    options.case_sensitive = true;
    watch.Reset();
    uedit::SearchOutcome outcome = uedit::LiteralSearch(
        file->data(), file->size(), needle, options, 200000, nullptr);
    const double search_ms = watch.ElapsedMs();
    const double throughput =
        search_ms > 0.0
            ? (static_cast<double>(file->size()) / (1024.0 * 1024.0)) /
                  (search_ms / 1000.0)
            : 0.0;
    wprintf(L"search:      \"%ls\" %.2f ms, hits=%llu, stored=%zu, %.0f MB/s\n",
            needle_wide.c_str(), search_ms, outcome.total_hits,
            outcome.hits.size(), throughput);
    if (!outcome.hits.empty()) {
      wprintf(L"first hit:   offset=%llu line=%llu\n", outcome.hits.front().offset,
              index.offset_to_line(outcome.hits.front().offset));
    }

    uedit::SearchOptions insensitive;
    insensitive.case_sensitive = false;
    watch.Reset();
    uedit::SearchOutcome folded = uedit::LiteralSearch(
        file->data(), file->size(), needle, insensitive, 200000, nullptr);
    wprintf(L"search(ci):  \"%ls\" %.2f ms, hits=%llu\n", needle_wide.c_str(),
            watch.ElapsedMs(), folded.total_hits);

    uedit::SearchRequest request;
    request.data = file->data();
    request.size = file->size();
    request.encoding = encoding.encoding;
    request.big_endian = encoding.big_endian();
    request.codepage = encoding.codepage;

    uedit::SearchOptions regex_options;
    regex_options.case_sensitive = true;
    regex_options.regex = true;
    watch.Reset();
    uedit::SearchOutcome regex_hits = uedit::SearchBuffer(
        request, "NEEDLE_\\d{4}", regex_options, 200000, nullptr);
    const double regex_ms = watch.ElapsedMs();
    const double regex_throughput =
        regex_ms > 0.0
            ? (static_cast<double>(file->size()) / (1024.0 * 1024.0)) /
                  (regex_ms / 1000.0)
            : 0.0;
    wprintf(L"regex:       NEEDLE_\\\\d{4} %.2f ms, hits=%llu, %.0f MB/s\n",
            regex_ms, regex_hits.total_hits, regex_throughput);
  } else {
    wprintf(L"search:      skipped (UTF-16 fixture)\n");
  }

  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    wprintf(L"memory:      private=%.1f MB, working=%.1f MB\n",
            static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0),
            static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0));
  }
  return 0;
}
