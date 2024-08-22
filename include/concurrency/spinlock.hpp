#pragma once

#include <atomic>
#include <cstddef>
#include <thread>

// Exponential backoff
class Spinner {
public:
  void Spin() {
    for (size_t i = 0; i < (1ul << counter_); ++i) {
      // https://stackoverflow.com/questions/50428450/what-does-asm-volatile-pause-memory-do
      asm volatile("pause\n" : : : "memory");
    }

    if (counter_ > kYieldCount) {
      std::this_thread::yield();
    } else {
      ++counter_;
    }
  }

private:
  static constexpr size_t kYieldCount = 12;
  size_t counter_{0};
};

inline void BusyWait(size_t max_cycles) {
  Spinner spinner;
  size_t current_cycles = 0;
  while (current_cycles++ < max_cycles) {
    spinner.Spin();
  }
}

// SpinLock without unnecessary cache invalidation
class SpinLock {
public:
  void Lock() {
    Spinner spinner;
    while (locked_.exchange(true, std::memory_order_acquire)) {
      while (locked_.load(std::memory_order_relaxed)) {
        spinner.Spin();
      }
    }
  }

  bool TryLock() { return !locked_.exchange(true); }

  void Unlock() { locked_.store(false, std::memory_order_release); }

  void lock() { Lock(); }

  bool try_lock() { return TryLock(); }

  void unlock() { Unlock(); }

private:
  std::atomic<bool> locked_{false};
};