// Shared PCRE2 worker implementation. Included from regex8.cpp / regex16.cpp
// with PCRE2_CODE_UNIT_WIDTH, UEDIT_REGEX_FN and UEDIT_UNIT_BYTES defined.

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

#include "common/stopwatch.h"
#include "search/search.h"

namespace uedit {
namespace {

constexpr uint64_t kInvalidOffset = ~0ull;
constexpr uint64_t kMaxLineScan = 64ull << 20;
constexpr uint64_t kChunkSize = 8ull << 20;

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) return {};
  int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(),
                      static_cast<int>(text.size()), &out[0], length);
  return out;
}

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (length <= 0) return {};
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                      static_cast<int>(text.size()), &out[0], length, nullptr,
                      nullptr);
  return out;
}

std::string WideToCodepage(const std::wstring& text, unsigned codepage) {
  if (text.empty()) return {};
  int length = WideCharToMultiByte(codepage, 0, text.c_str(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (length <= 0) return {};
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(codepage, 0, text.c_str(),
                      static_cast<int>(text.size()), &out[0], length, nullptr,
                      nullptr);
  return out;
}

#if PCRE2_CODE_UNIT_WIDTH == 8
std::string EscapeLiteral(std::string_view text) {
  std::string out;
  out.reserve(text.size() * 2);
  for (char c : text) {
    if (std::strchr("\\^$.[]|()?*+{}", c) != nullptr) out.push_back('\\');
    out.push_back(c);
  }
  return out;
}
#else
std::wstring EscapeLiteral(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size() * 2);
  for (wchar_t c : text) {
    if (std::wcschr(L"\\^$.[]|()?*+{}", c) != nullptr) out.push_back(L'\\');
    out.push_back(c);
  }
  return out;
}
#endif

bool IsLineStart(const uint8_t* data, uint64_t size, uint64_t pos,
                 bool big_endian) {
#if PCRE2_CODE_UNIT_WIDTH == 8
  (void)size;
  (void)big_endian;
  return pos == 0 || data[pos - 1] == '\n';
#else
  (void)size;
  if (pos == 0) return true;
  if (pos < 2) return false;
  if (big_endian) {
    return data[pos - 2] == 0x00 && data[pos - 1] == 0x0A;
  }
  return data[pos - 2] == 0x0A && data[pos - 1] == 0x00;
#endif
}

bool IsLineEnd(const uint8_t* data, uint64_t size, uint64_t pos,
               bool big_endian) {
  if (pos >= size) return true;
  return IsLineStart(data, size, pos, big_endian);
}

// Returns the offset just past the next line separator at/after `from`, or
// kInvalidOffset when none was found within the scan limit.
uint64_t FindLineBoundary(const uint8_t* data, uint64_t size, uint64_t from,
                          bool big_endian) {
  const uint64_t limit = std::min(size, from + kMaxLineScan);
  if (from >= limit) return kInvalidOffset;
#if PCRE2_CODE_UNIT_WIDTH == 8
  (void)big_endian;
  const void* hit = std::memchr(data + from, '\n',
                                static_cast<size_t>(limit - from));
  if (hit == nullptr) return kInvalidOffset;
  return static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data) + 1;
#else
  uint64_t cursor = from & ~1ull;
  while (cursor + 1 < limit) {
    const void* hit = std::memchr(data + cursor, 0x0A,
                                  static_cast<size_t>(limit - cursor));
    if (hit == nullptr) return kInvalidOffset;
    const uint64_t offset =
        static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data);
    if (big_endian) {
      if ((offset & 1) == 1 && offset > 0 && data[offset - 1] == 0x00) {
        return offset + 1;
      }
    } else {
      if ((offset & 1) == 0 && offset + 1 < limit &&
          data[offset + 1] == 0x00) {
        return offset + 2;
      }
    }
    cursor = offset + 1;
  }
  return kInvalidOffset;
#endif
}

}  // namespace

SearchOutcome UEDIT_REGEX_FN(const SearchRequest& request,
                             std::string_view pattern_utf8,
                             const SearchOptions& options, size_t max_hits,
                             CancelToken* cancel,
                             std::atomic<uint64_t>* scanned_progress,
                             bool literal_pattern) {
  SearchOutcome outcome;
  Stopwatch stopwatch;
  if (request.data == nullptr || request.size == 0) {
    outcome.seconds = stopwatch.ElapsedSeconds();
    return outcome;
  }

  // ---- pattern ----
  std::string pattern8;
  std::wstring pattern16;
#if PCRE2_CODE_UNIT_WIDTH == 8
  const bool utf8 = request.encoding == TextEncoding::Utf8;
  if (utf8) {
    pattern8 = literal_pattern ? EscapeLiteral(pattern_utf8)
                               : std::string(pattern_utf8);
  } else {
    pattern8 = WideToCodepage(Utf8ToWide(pattern_utf8), request.codepage);
    if (pattern8.empty()) {
      outcome.pattern_error = true;
      outcome.error_message = "模式无法转换为当前 ANSI 编码";
      outcome.seconds = stopwatch.ElapsedSeconds();
      return outcome;
    }
  }
  if (options.whole_word) pattern8 = "\\b(?:" + pattern8 + ")\\b";
  uint32_t compile_options = PCRE2_MULTILINE;
  if (utf8) {
    compile_options |= PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
  }
#else
  pattern16 = Utf8ToWide(pattern_utf8);
  if (literal_pattern) pattern16 = EscapeLiteral(pattern16);
  if (options.whole_word) pattern16 = L"\\b(?:" + pattern16 + L")\\b";
  uint32_t compile_options = PCRE2_MULTILINE | PCRE2_UTF | PCRE2_UCP;
#endif
  if (!options.case_sensitive) compile_options |= PCRE2_CASELESS;

#if PCRE2_CODE_UNIT_WIDTH == 8
  PCRE2_SPTR pattern_ptr =
      reinterpret_cast<PCRE2_SPTR>(pattern8.data());
  const PCRE2_SIZE pattern_length = pattern8.size();
#else
  PCRE2_SPTR pattern_ptr =
      reinterpret_cast<PCRE2_SPTR>(pattern16.data());
  const PCRE2_SIZE pattern_length = pattern16.size();
#endif

  int error_code = 0;
  PCRE2_SIZE error_offset = 0;
  pcre2_code* code =
      pcre2_compile(pattern_ptr, pattern_length, compile_options, &error_code,
                    &error_offset, nullptr);
  if (code == nullptr) {
    outcome.pattern_error = true;
    PCRE2_UCHAR buffer[256]{};
    if (pcre2_get_error_message(error_code, buffer, 256) >= 0) {
#if PCRE2_CODE_UNIT_WIDTH == 8
      outcome.error_message.assign(reinterpret_cast<const char*>(buffer));
#else
      outcome.error_message = WideToUtf8(
          std::wstring(reinterpret_cast<const wchar_t*>(buffer)));
#endif
    } else {
      outcome.error_message = "正则表达式编译失败";
    }
    outcome.seconds = stopwatch.ElapsedSeconds();
    return outcome;
  }
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);

  // ---- segmentation ----
  unsigned hardware = std::thread::hardware_concurrency();
  if (hardware == 0) hardware = 4;
  unsigned workers = std::min<unsigned>(hardware, 8);
  const uint64_t minimum_slice = 1ull << 20;
  workers = std::min<unsigned>(
      workers, static_cast<unsigned>(
                   std::max<uint64_t>(1, request.size / minimum_slice)));

  std::vector<uint64_t> bounds(workers + 1, 0);
  bounds[0] = 0;
  bounds[workers] = request.size;
  for (unsigned w = 1; w < workers; ++w) {
    uint64_t approx = request.size * w / workers;
#if PCRE2_CODE_UNIT_WIDTH == 16
    approx &= ~1ull;
#endif
    uint64_t boundary =
        FindLineBoundary(request.data, request.size, approx, request.big_endian);
    if (boundary == kInvalidOffset || boundary > request.size) boundary = approx;
    if (boundary <= bounds[w - 1]) boundary = approx;
    if (boundary <= bounds[w - 1]) boundary = bounds[w - 1];
    if (boundary > request.size) boundary = request.size;
#if PCRE2_CODE_UNIT_WIDTH == 16
    boundary &= ~1ull;
#endif
    bounds[w] = boundary;
  }

  std::vector<std::vector<SearchHit>> per_worker(workers);
  std::atomic<uint64_t> scanned{0};
  std::atomic<uint64_t> hit_count{0};
  std::atomic<bool> truncated{false};
  std::atomic<bool> cancelled{false};

  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (unsigned w = 0; w < workers; ++w) {
    const uint64_t range_begin = bounds[w];
    const uint64_t range_end = bounds[w + 1];
    if (range_begin >= range_end) continue;
    threads.emplace_back([&, w, range_begin, range_end]() {
      pcre2_match_data* match_data =
          pcre2_match_data_create_from_pattern(code, nullptr);
      pcre2_match_context* match_context = pcre2_match_context_create(nullptr);
      if (match_data == nullptr) {
        if (match_context != nullptr) pcre2_match_context_free(match_context);
        return;
      }
      if (match_context != nullptr) {
        pcre2_set_match_limit(match_context, 10000000);
        pcre2_set_depth_limit(match_context, 2000);
      }

      std::vector<SearchHit>& local = per_worker[w];
      std::vector<uint16_t> swapped;
      uint64_t position = range_begin;
      while (position < range_end) {
        if (cancel != nullptr && cancel->cancelled()) {
          cancelled.store(true, std::memory_order_relaxed);
          break;
        }
        uint64_t chunk_end = std::min(range_end, position + kChunkSize);
        if (chunk_end < range_end) {
          const uint64_t boundary = FindLineBoundary(
              request.data, request.size, chunk_end, request.big_endian);
          if (boundary != kInvalidOffset && boundary <= range_end) {
            chunk_end = boundary;
          }
        }
#if PCRE2_CODE_UNIT_WIDTH == 16
        chunk_end &= ~1ull;
        if (chunk_end <= position) chunk_end = std::min(range_end, position + 2);
#endif

        PCRE2_SPTR subject = nullptr;
        PCRE2_SIZE subject_length = 0;
        const uint64_t byte_length = chunk_end - position;
#if PCRE2_CODE_UNIT_WIDTH == 8
        subject =
            reinterpret_cast<PCRE2_SPTR>(request.data + position);
        subject_length = static_cast<PCRE2_SIZE>(byte_length);
#else
        if (request.big_endian) {
          const size_t units = static_cast<size_t>(byte_length / 2);
          swapped.resize(units);
          const uint8_t* source = request.data + position;
          for (size_t i = 0; i < units; ++i) {
            swapped[i] = static_cast<uint16_t>((source[i * 2] << 8) |
                                               source[i * 2 + 1]);
          }
          subject = reinterpret_cast<PCRE2_SPTR>(swapped.data());
          subject_length = static_cast<PCRE2_SIZE>(units);
        } else {
          subject = reinterpret_cast<PCRE2_SPTR>(request.data + position);
          subject_length = static_cast<PCRE2_SIZE>(byte_length / 2);
        }
#endif

        uint32_t match_options = 0;
        if (!IsLineStart(request.data, request.size, position,
                         request.big_endian)) {
          match_options |= PCRE2_NOTBOL;
        }
        if (chunk_end < request.size &&
            !IsLineEnd(request.data, request.size, chunk_end,
                       request.big_endian)) {
          match_options |= PCRE2_NOTEOL;
        }

        PCRE2_SIZE offset = 0;
        uint32_t local_matches = 0;
        while (offset <= subject_length) {
          const int rc = pcre2_match(code, subject, subject_length, offset,
                                     match_options, match_data, match_context);
          if (rc == PCRE2_ERROR_NOMATCH) break;
          if (rc < 0) break;
          PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
          const uint64_t unit_start = ovector[0];
          const uint64_t unit_end = ovector[1];
          const uint64_t byte_offset =
              position + unit_start * UEDIT_UNIT_BYTES;
          const uint64_t match_bytes =
              (unit_end - unit_start) * UEDIT_UNIT_BYTES;
          const uint64_t ordinal =
              hit_count.fetch_add(1, std::memory_order_relaxed);
          if (ordinal < max_hits) {
            SearchHit hit;
            hit.offset = byte_offset;
            hit.length = static_cast<uint32_t>(match_bytes);
            local.push_back(hit);
          } else {
            truncated.store(true, std::memory_order_relaxed);
          }
          offset = (unit_end > unit_start) ? unit_end : unit_start + 1;
          if ((++local_matches & 0x3FF) == 0 && cancel != nullptr &&
              cancel->cancelled()) {
            cancelled.store(true, std::memory_order_relaxed);
            break;
          }
        }

        const uint64_t completed =
            scanned.fetch_add(byte_length, std::memory_order_relaxed) +
            byte_length;
        if (scanned_progress != nullptr) {
          scanned_progress->store(completed, std::memory_order_relaxed);
        }
        if (cancelled.load(std::memory_order_relaxed)) break;
        position = chunk_end;
      }

      pcre2_match_data_free(match_data);
      if (match_context != nullptr) pcre2_match_context_free(match_context);
    });
  }
  for (std::thread& thread : threads) thread.join();
  pcre2_code_free(code);

  size_t stored = 0;
  for (const auto& worker_hits : per_worker) stored += worker_hits.size();
  outcome.hits.reserve(stored);
  for (auto& worker_hits : per_worker) {
    for (const SearchHit& hit : worker_hits) outcome.hits.push_back(hit);
  }
  std::sort(outcome.hits.begin(), outcome.hits.end(),
            [](const SearchHit& a, const SearchHit& b) {
              return a.offset < b.offset;
            });

  outcome.scanned_bytes = scanned.load(std::memory_order_relaxed);
  outcome.total_hits = hit_count.load(std::memory_order_relaxed);
  outcome.cancelled = cancelled.load(std::memory_order_relaxed);
  outcome.truncated = truncated.load(std::memory_order_relaxed);
  outcome.seconds = stopwatch.ElapsedSeconds();
  if (scanned_progress != nullptr) {
    scanned_progress->store(request.size, std::memory_order_relaxed);
  }
  return outcome;
}

}  // namespace uedit
