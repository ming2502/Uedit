#include "file/line_index.h"

#include <algorithm>
#include <cstring>

namespace uedit {

void LineIndex::Prepare(const uint8_t* data, uint64_t size, uint64_t bom,
                        int unit_size, bool big_endian) {
  data_ = data;
  size_ = size;
  bom_ = std::min(bom, size);
  unit_ = unit_size == 2 ? 2 : 1;
  big_endian_ = big_endian;

  uint64_t blocks = size_ / kBlock + 2;
  checkpoints_.assign(static_cast<size_t>(blocks), Checkpoint{0, 0});
  checkpoints_[0].offset = bom_;
  checkpoints_[0].line = 1;

  valid_checkpoints_.store(1, std::memory_order_release);
  lines_counted_.store(1, std::memory_order_relaxed);
  scanned_.store(0, std::memory_order_relaxed);
  total_exact_.store(0, std::memory_order_relaxed);
  complete_.store(false, std::memory_order_release);
}

void LineIndex::Build(CancelToken* cancel) {
  const uint64_t capacity = static_cast<uint64_t>(checkpoints_.size());
  uint64_t line = 1;
  uint64_t pos = bom_;
  uint64_t next_block = 1;
  uint64_t published = 1;
  uint64_t next_progress = 8ull << 20;
  uint32_t ticks = 0;

  while (pos < size_) {
    if (((++ticks) & 0xFFF) == 0 && cancel != nullptr && cancel->cancelled()) {
      break;
    }
    uint64_t separator = find_separator(pos, size_);
    if (separator == kInvalid) break;
    uint64_t next = separator + static_cast<uint64_t>(unit_);
    ++line;

    while (next_block < capacity && next_block * kBlock <= next) {
      checkpoints_[next_block].offset = next;
      checkpoints_[next_block].line = line;
      ++next_block;
    }
    if (next_block != published) {
      valid_checkpoints_.store(next_block, std::memory_order_release);
      published = next_block;
    }

    pos = next;
    if (pos >= next_progress) {
      scanned_.store(pos, std::memory_order_relaxed);
      lines_counted_.store(line, std::memory_order_relaxed);
      next_progress = pos + (8ull << 20);
    }
  }

  const uint64_t tail_start = std::min(pos, size_);
  while (next_block < capacity) {
    checkpoints_[next_block].offset = tail_start;
    checkpoints_[next_block].line = line;
    ++next_block;
  }
  valid_checkpoints_.store(next_block, std::memory_order_release);
  scanned_.store(size_, std::memory_order_relaxed);
  lines_counted_.store(line, std::memory_order_relaxed);
  total_exact_.store(line, std::memory_order_relaxed);
  complete_.store(true, std::memory_order_release);
}

uint64_t LineIndex::find_separator(uint64_t pos, uint64_t limit) const {
  if (limit > size_) limit = size_;
  if (pos >= limit) return kInvalid;

  if (unit_ == 1) {
    const void* hit = std::memchr(data_ + pos, '\n',
                                  static_cast<size_t>(limit - pos));
    if (hit == nullptr) return kInvalid;
    return static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data_);
  }

  uint64_t cursor = pos;
  while (cursor + 1 < limit) {
    const void* hit = std::memchr(data_ + cursor, 0x0A,
                                  static_cast<size_t>(limit - cursor));
    if (hit == nullptr) return kInvalid;
    uint64_t offset =
        static_cast<uint64_t>(static_cast<const uint8_t*>(hit) - data_);
    if (big_endian_) {
      if ((offset & 1) == 1 && offset > 0 && data_[offset - 1] == 0x00) {
        return offset - 1;
      }
    } else {
      if ((offset & 1) == 0 && offset + 1 < limit && data_[offset + 1] == 0x00) {
        return offset;
      }
    }
    cursor = offset + 1;
  }
  return kInvalid;
}

uint64_t LineIndex::count_separators(uint64_t from, uint64_t to) const {
  uint64_t count = 0;
  uint64_t pos = from;
  while (pos < to) {
    uint64_t separator = find_separator(pos, to);
    if (separator == kInvalid) break;
    ++count;
    pos = separator + static_cast<uint64_t>(unit_);
  }
  return count;
}

uint64_t LineIndex::line_start(uint64_t line) const {
  if (line <= 1 || data_ == nullptr) return bom_;

  uint64_t valid = valid_checkpoints_.load(std::memory_order_acquire);
  if (valid == 0) return bom_;
  size_t count = static_cast<size_t>(
      std::min<uint64_t>(valid, checkpoints_.size()));

  size_t lo = 0;
  size_t hi = count;
  if (checkpoints_[0].line > line) return bom_;
  while (lo + 1 < hi) {
    size_t mid = (lo + hi) / 2;
    if (checkpoints_[mid].line <= line) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  uint64_t pos = checkpoints_[lo].offset;
  uint64_t current = checkpoints_[lo].line;
  while (current < line && pos < size_) {
    uint64_t separator = find_separator(pos, size_);
    if (separator == kInvalid) return size_;
    pos = separator + static_cast<uint64_t>(unit_);
    ++current;
  }
  return pos;
}

uint64_t LineIndex::line_end(uint64_t line) const {
  if (line >= total_lines() && complete()) return size_;
  return line_start(line + 1);
}

uint64_t LineIndex::offset_to_line(uint64_t offset) const {
  if (data_ == nullptr) return 1;
  if (offset < bom_) offset = bom_;
  if (offset > size_) offset = size_;

  uint64_t valid = valid_checkpoints_.load(std::memory_order_acquire);
  if (valid == 0) return 1;
  size_t count = static_cast<size_t>(
      std::min<uint64_t>(valid, checkpoints_.size()));

  size_t lo = 0;
  size_t hi = count;
  while (lo + 1 < hi) {
    size_t mid = (lo + hi) / 2;
    if (checkpoints_[mid].offset <= offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  const Checkpoint& base = checkpoints_[lo];
  if (base.offset > offset) return base.line;
  return base.line + count_separators(base.offset, offset);
}

bool LineIndex::next_line_start(uint64_t pos, uint64_t max_scan,
                                uint64_t* next) const {
  if (next == nullptr || data_ == nullptr || pos >= size_) return false;
  uint64_t limit = std::min(size_, pos + max_scan);
  uint64_t separator = find_separator(pos, limit);
  if (separator == kInvalid) return false;
  uint64_t value = separator + static_cast<uint64_t>(unit_);
  if (value > size_) return false;
  *next = value;
  return true;
}

uint64_t LineIndex::estimated_lines() const {
  if (complete()) return total_exact_.load(std::memory_order_relaxed);
  uint64_t scanned = scanned_.load(std::memory_order_relaxed);
  uint64_t lines = lines_counted_.load(std::memory_order_relaxed);
  if (scanned < 4096 || lines == 0) {
    return std::max<uint64_t>(size_ / 96, 1);
  }
  double average = static_cast<double>(scanned) / static_cast<double>(lines);
  if (average < 1.0) average = 1.0;
  double estimate =
      static_cast<double>(lines) + static_cast<double>(size_ - scanned) / average;
  return std::max<uint64_t>(static_cast<uint64_t>(estimate), 1);
}

double LineIndex::progress() const {
  if (size_ == 0 || complete()) return 1.0;
  return static_cast<double>(scanned_.load(std::memory_order_relaxed)) /
         static_cast<double>(size_);
}

}  // namespace uedit
