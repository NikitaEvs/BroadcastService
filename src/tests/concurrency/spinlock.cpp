#include <gtest/gtest.h>
#include <mutex>

#include "concurrency/spinlock.hpp"
#include "runtime/thread_pool.hpp"

using Mutex = SpinLock;

TEST(SpinLock, SmokeTest) {
  Mutex mutex;
  std::lock_guard lock(mutex);
}

class Counter {
public:
  void Increment() {
    std::lock_guard lock(mutex_);
    ++counter_;
  }

  size_t Get() {
    std::lock_guard lock(mutex_);
    return counter_;
  }

private:
  Mutex mutex_;
  size_t counter_ = 0; // guarded by mutex
};

TEST(SpinLock, Concurrency) {
  Counter counter;

  ThreadPool thread_pool(5);
  thread_pool.Start();

  constexpr size_t num_workers = 5;
  constexpr size_t num_increments = 1'000'000;

  for (size_t worker = 0; worker < num_workers; ++worker) {
    thread_pool.Execute(Lambda::Create([&counter, num_increments]() {
      for (size_t i = 0; i < num_increments; ++i) {
        counter.Increment();
      }
    }));
  }

  thread_pool.Wait();

  ASSERT_EQ(num_workers * num_increments, counter.Get());

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}