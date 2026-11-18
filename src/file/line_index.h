#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "common/cancel_token.h"

namespace uedit {

// Sparse line index: one checkpoint per 1 MiB block, holding the first line
// start at/after the block boundary and that line's number. Building runs on a
// worker thread and publishes checkpoints progressively, so lookups are safe
// while indexing is still in progress.
class LineIndex {
 public:
  static constexpr uint64_t kBlock = 1ull << 20;
  static constexpr uint64_t kInvalid = ~0ull;

  // Must be called on the UI thread with no Build in flight.
  void Prepare(const uint8_t* data, uint64_t size, uint64_t bom,
               int unit_size, bool big_endian);
  // Worker thread.
  void Build(CancelToken* cancel);

  uint64_t size() const { return size_; }
  bool complete() const { return complete_.load(std::memory_order_acquire); }
  uint64_t total_lines() const {
    return complete() ? total_exact_.load(std::memory_order_relaxed)
                      : lines_counted_.load(std::memory_order_relaxed);
  }
  uint64_t estimated_lines() const;
  double progress() const;

  // 1-based line number to byte offset. Safe during Build.
  uint64_t line_start(uint64_t line) const;
  uint64_t line_end(uint64_t line) const;
  uint64_t offset_to_line(uint64_t offset) const;

  // Next line start after `pos`, scanning at most `max_scan` bytes.
  bool next_line_start(uint64_t pos, uint64_t max_scan, uint64_t* next) const;

 private:
  struct Checkpoint {
    uint64_t offset;
    uint64_t line;
  };

  uint64_t find_separator(uint64_t pos, uint64_t limit) const;
  uint64_t count_separators(uint64_t from, uint64_t to) const;

  const uint8_t* data_ = nullptr;
  uint64_t size_ = 0;
  uint64_t bom_ = 0;
  int unit_ = 1;
  bool big_endian_ = false;

  std::vector<Checkpoint> checkpoints_;
  std::atomic<uint64_t> valid_checkpoints_{0};
  std::atomic<uint64_t> lines_counted_{1};
  std::atomic<uint64_t> scanned_{0};
  std::atomic<uint64_t> total_exact_{0};
  std::atomic<bool> complete_{false};
};

}  // namespace uedit
