#include <gtest/gtest.h>

#include "concurrency/fiber_sync.hpp"
#include "runtime/api.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"

TEST(FiberSync, EventSmokeTest) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  FiberOneShotEvent event;
  bool flag = false;
  bool output_flag = false;

  Go(&thread_pool, [&event, &flag, &output_flag]() {
    event.Wait();
    output_flag = flag;
  });

  Go(&thread_pool, [&event, &flag]() {
    flag = true;
    event.Fire();
  });

  thread_pool.Wait();

  ASSERT_TRUE(flag);
  ASSERT_TRUE(output_flag);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(FiberSync, EventMultipleWaiters) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  FiberOneShotEvent event;
  bool flag = false;
  std::atomic<bool> output_flag;

  constexpr size_t num_waiters = 100;
  for (size_t i = 0; i < num_waiters; ++i) {
    Go(&thread_pool, [&event, &flag, &output_flag]() {
      event.Wait();
      output_flag.store(flag);
    });
  }

  Go(&thread_pool, [&event, &flag]() {
    flag = true;
    event.Fire();
  });

  thread_pool.Wait();

  ASSERT_TRUE(flag);
  ASSERT_TRUE(output_flag.load());

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(FiberSync, MutexShadowTest) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  FiberMutex mutex;

  bool flag = false;

  Go(&thread_pool, [&mutex, &flag]() {
    std::lock_guard lock(mutex);
    flag = true;
  });

  Go(&thread_pool, [&mutex, &flag]() {
    std::lock_guard lock(mutex);
    flag = true;
  });

  thread_pool.Wait();

  ASSERT_TRUE(flag);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(FiberSync, WaitGroup) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  constexpr size_t num_workers = 5;

  std::atomic<bool> flag{false};
  std::atomic<bool> output;

  Go(&thread_pool, [&flag, &output]() {
    FiberWaitGroup wg;

    for (uint32_t i = 0; i < num_workers; ++i) {
      wg.Add();
      Go([&flag, &wg]() {
        flag.store(true);
        wg.Done();
      });
    }

    wg.Wait();
    output.store(flag.load());
  });

  thread_pool.Wait();

  ASSERT_TRUE(output.load());

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}
