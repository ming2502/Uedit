#include "search/literal_search.h"

#include <algorithm>
#include <cstring>
#include <thread>

#include "common/stopwatch.h"

namespace uedit {
namespace {

inline uint8_t Fold(uint8_t c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c + 32) : c;
}

inline bool IsWordByte(uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

class Matcher {
 public:
  Matcher(std::string_view needle, bool case_sensitive, bool whole_word)
      : fold_(!case_sensitive), whole_word_(whole_word) {
    pattern_.reserve(needle.size());
    for (char ch : needle) {
      uint8_t byte = static_cast<uint8_t>(ch);
      pattern_.push_back(fold_ ? Fold(byte) : byte);
    }
    const size_t length = pattern_.size();
    for (size_t i = 0; i < 256; ++i) {
      skip_[i] = static_cast<uint8_t>(std::min<size_t>(length, 255));
    }
    for (size_t i = 0; i + 1 < length; ++i) {
      skip_[pattern_[i]] =
          static_cast<uint8_t>(std::min<size_t>(length - 1 - i, 255));
    }
  }

  bool empty() const { return pattern_.empty(); }
  size_t size() const { return pattern_.size(); }

  // Finds a match in [from, scan_end); a match must fit entirely inside
  // scan_end. Word boundaries are evaluated against the whole buffer.
  bool find(const uint8_t* data, uint64_t from, uint64_t scan_end,
            uint64_t file_size, uint64_t* hit_offset) const {
    const size_t length = pattern_.size();
    if (length == 0) return false;
    uint64_t i = from;
    while (i + length <= scan_end) {
      if (i + length > file_size) break;
      const uint8_t* candidate = data + i;
      uint8_t last = fold_ ? Fold(candidate[length - 1]) : candidate[length - 1];
      if (last == pattern_[length - 1]) {
        bool equal = true;
        for (size_t k = 0; k + 1 < length; ++k) {
          uint8_t value = fold_ ? Fold(candidate[k]) : candidate[k];
          if (value != pattern_[k]) {
            equal = false;
            break;
          }
        }
        if (equal && boundary_ok(data, file_size, i, length)) {
          *hit_offset = i;
          return true;
        }
      }
      i += skip_[last];
    }
    return false;
  }

 private:
  bool boundary_ok(const uint8_t* data, uint64_t file_size, uint64_t offset,
                   size_t length) const {
    if (!whole_word_) return true;
    if (offset > 0 && IsWordByte(data[offset - 1])) return false;
    if (offset + length < file_size && IsWordByte(data[offset + length])) {
      return false;
    }
    return true;
  }

  std::vector<uint8_t> pattern_;
  uint8_t skip_[256];
  bool fold_;
  bool whole_word_;
};

}  // namespace

SearchOutcome LiteralSearch(const uint8_t* data, uint64_t size,
                            std::string_view needle,
                            const SearchOptions& options, size_t max_hits,
                            CancelToken* cancel,
                            std::atomic<uint64_t>* scanned_progress) {
  SearchOutcome outcome;
  Stopwatch stopwatch;
  (void)options.regex;

  Matcher matcher(needle, options.case_sensitive, options.whole_word);
  if (matcher.empty() || size == 0) {
    outcome.seconds = stopwatch.ElapsedSeconds();
    return outcome;
  }
  if (scanned_progress != nullptr) {
    scanned_progress->store(0, std::memory_order_relaxed);
  }

  const uint64_t chunk = 8ull << 20;
  unsigned hardware = std::thread::hardware_concurrency();
  if (hardware == 0) hardware = 4;
  unsigned workers = std::min<unsigned>(hardware, 8);
  workers = std::min<unsigned>(workers, static_cast<unsigned>(
                                           std::max<uint64_t>(1, (size + chunk - 1) / chunk)));

  std::vector<std::vector<SearchHit>> per_worker(workers);
  std::atomic<uint64_t> scanned{0};
  std::atomic<uint64_t> hit_count{0};
  std::atomic<bool> truncated{false};
  std::atomic<bool> cancelled{false};
  const size_t pattern_length = matcher.size();

  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (unsigned worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&, worker]() {
      std::vector<SearchHit>& local = per_worker[worker];
      for (uint64_t start = static_cast<uint64_t>(worker) * chunk;
           start < size; start += chunk * workers) {
        if (cancel != nullptr && cancel->cancelled()) {
          cancelled.store(true, std::memory_order_relaxed);
          break;
        }
        uint64_t end = std::min(size, start + chunk);
        uint64_t scan_end =
            std::min(size, end + static_cast<uint64_t>(pattern_length) - 1);
        uint64_t position = start;
        while (position < scan_end) {
          uint64_t hit = 0;
          if (!matcher.find(data, position, scan_end, size, &hit)) break;
          if (hit >= end) break;
          uint64_t ordinal = hit_count.fetch_add(1, std::memory_order_relaxed);
          if (ordinal < max_hits) {
            SearchHit record;
            record.offset = hit;
            record.length = static_cast<uint32_t>(pattern_length);
            local.push_back(record);
          } else {
            truncated.store(true, std::memory_order_relaxed);
          }
          position = hit + 1;
        }
        uint64_t done = scanned.fetch_add(end - start,
                                          std::memory_order_relaxed) +
                        (end - start);
        if (scanned_progress != nullptr) {
          scanned_progress->store(done, std::memory_order_relaxed);
        }
      }
      if (cancel != nullptr && cancel->cancelled()) {
        cancelled.store(true, std::memory_order_relaxed);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();

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
  outcome.seconds = stopwatch.ElapsedSeconds();
  outcome.cancelled = cancelled.load(std::memory_order_relaxed);
  outcome.truncated = truncated.load(std::memory_order_relaxed);
  if (scanned_progress != nullptr) {
    scanned_progress->store(size, std::memory_order_relaxed);
  }
  return outcome;
}

}  // namespace uedit
