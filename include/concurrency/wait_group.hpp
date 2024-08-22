#pragma once

#include <atomic>

#include "concurrency/futex.hpp"

class WaitGroup {
public:
  void Add(uint32_t num = 1) { counter_.fetch_add(num); }

  void Done(uint32_t num = 1) {
    auto key = AtomicAddr(counter_);
    if (counter_.fetch_sub(num) - num == 0) {
      AtomicWakeAll(key);
    }
  }

  void Wait() {
    auto count = counter_.load();
    while (count != 0) {
      AtomicWait(counter_, count);
      count = counter_.load();
    }
  }

private:
  std::atomic<uint32_t> counter_{0};
};
