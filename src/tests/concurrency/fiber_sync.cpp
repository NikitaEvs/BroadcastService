#include <gtest/gtest.h>

#include "concurrency/fiber_sync.hpp"
#include "runtime/api.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"

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
