#pragma once

#include <atomic>

namespace uedit {

class CancelToken {
 public:
  void Cancel() { flag_.store(true, std::memory_order_relaxed); }
  bool cancelled() const { return flag_.load(std::memory_order_relaxed); }
  void Reset() { flag_.store(false, std::memory_order_relaxed); }

 private:
  std::atomic<bool> flag_{false};
};

}  // namespace uedit
