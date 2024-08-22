#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "concurrency/fiber_sync.hpp"
#include "runtime/api.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"

using namespace std::chrono_literals;

TEST(ThreadPool, SmokeTest) {
  ThreadPool pool(5);
  pool.Start();
  pool.SignalStop();
  pool.WaitForStop();
}

TEST(ThreadPool, RunTasks) {
  ThreadPool pool(5);
  pool.Start();

  bool flag = false;

  pool.Execute(Lambda::Create([&]() noexcept { flag = true; }));

  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_TRUE(flag);
}

TEST(ThreadPool, RunTasksParallel) {
  ThreadPool pool(5);
  pool.Start();

  std::atomic<uint32_t> counter{0};

  for (size_t i = 0; i < 5; ++i) {
    pool.Execute(Lambda::Create([&]() {
      std::this_thread::sleep_for(500ms);
      counter.fetch_add(1);
    }));
  }

  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_EQ(5, counter.load());
}

TEST(ThreadPool, ExecuteFromTask) {
  ThreadPool pool(5);
  pool.Start();

  bool flag = false;

  pool.Execute(Lambda::Create([&flag]() {
    ThreadPool::Current()->Execute(
        Lambda::Create([&flag]() noexcept { flag = true; }));
  }));

  std::this_thread::sleep_for(100ms);

  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_TRUE(flag);
}

TEST(ThreadPool, FibersSmokeTest) {
  ThreadPool pool(5);
  pool.Start();

  bool flag = false;

  Go(&pool, [&flag]() noexcept { flag = true; });

  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_TRUE(flag);
}

TEST(ThreadPool, InternalGo) {
  ThreadPool pool(5);
  pool.Start();

  bool flag = false;

  Go(&pool, [&flag]() { Go([&flag]() noexcept { flag = true; }); });

  std::this_thread::sleep_for(100ms);
  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_TRUE(flag);
}

TEST(ThreadPool, MultipleFibers) {
  ThreadPool pool(5);
  pool.Start();

  std::atomic<uint32_t> counter{0};
  const size_t num_producers = 1000;
  const size_t num_additions = 1000;

  Go(&pool, [&]() {
    FiberWaitGroup wg;
    wg.Add(num_producers);
    for (size_t i = 0; i < num_producers; ++i) {
      Go(&pool, [&]() {
        for (size_t addition = 0; addition < num_additions; ++addition) {
          counter.fetch_add(1);
          Yield();
        }
        wg.Done();
      });
    }
    wg.Wait();
  });

  pool.Wait();
  pool.SignalStop();
  pool.WaitForStop();

  ASSERT_EQ(num_producers * num_additions, counter.load());
}
