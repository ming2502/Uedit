#pragma once

#include <chrono>

namespace uedit {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  void Reset() { start_ = Clock::now(); }

  double ElapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - start_).count();
  }

  double ElapsedMs() const { return ElapsedSeconds() * 1000.0; }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

}  // namespace uedit
