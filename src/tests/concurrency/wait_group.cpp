#include "concurrency/wait_group.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST(WaitGroup, SmokeTest) {
  WaitGroup wg;
  wg.Add();
  wg.Done();
  wg.Wait();
}

TEST(WaitGroup, Wait) {
  const uint32_t num_threads = 5;
  std::vector<std::thread> threads;

  WaitGroup wg;
  std::atomic<bool> flag{false};

  for (uint32_t i = 0; i < num_threads; ++i) {
    wg.Add();
    threads.emplace_back([&]() {
      std::this_thread::sleep_for(200ms);
      flag.store(true);
      wg.Done();
    });
  }

  ASSERT_FALSE(flag.load());
  wg.Wait();
  ASSERT_TRUE(flag.load());

  for (auto &thread : threads) {
    thread.join();
  }
}

TEST(WaitGroup, ConcurrentWait) {
  const uint32_t num_waiters = 10;
  const uint32_t num_producers = 10;

  std::vector<std::thread> waiters;
  std::vector<std::thread> producers;

  WaitGroup wg;
  std::atomic<uint32_t> counter{0};

  wg.Add(num_producers);

  for (uint32_t i = 0; i < num_waiters; ++i) {
    waiters.emplace_back([&]() {
      wg.Wait();
      ASSERT_EQ(num_producers, counter.load());
    });
  }

  for (uint32_t i = 0; i < num_producers; ++i) {
    producers.emplace_back([i, &counter, &wg]() {
      std::this_thread::sleep_for(100ms * i);
      counter.fetch_add(1);
      wg.Done();
    });
  }

  for (auto &thread : producers) {
    thread.join();
  }

  for (auto &thread : waiters) {
    thread.join();
  }
}
