#include <gtest/gtest.h>

#include "concurrency/contract.hpp"
#include "runtime/api.hpp"
#include "runtime/async_mutex.hpp"
#include "runtime/thread_pool.hpp"

TEST(AsyncMutex, ShadowTest) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  AsyncMutex async_mutex(&thread_pool);

  bool flag = false;

  async_mutex.Execute(Lambda::Create([&flag]() noexcept { flag = true; }));

  thread_pool.Wait();

  ASSERT_TRUE(flag);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(AsyncMutex, Counter) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  AsyncMutex async_mutex(&thread_pool);

  size_t counter = 0;

  constexpr size_t num_iterations = 1'000'000;

  for (size_t i = 0; i < num_iterations; ++i) {
    async_mutex.Execute(Lambda::Create([&counter, i]() noexcept {
      ASSERT_EQ(counter, i);
      ++counter;
    }));
  }

  thread_pool.Wait();

  ASSERT_EQ(num_iterations, counter);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(AsyncMutex, Fibers) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  AsyncMutex async_mutex(&thread_pool);

  std::vector<size_t> values;

  constexpr size_t num_producers = 1000;
  constexpr size_t num_consumers = num_producers;
  constexpr size_t number = 5;

  size_t result = 0;

  for (size_t producer = 0; producer < num_producers; ++producer) {
    Go(&async_mutex,
       [&values, number]() noexcept { values.push_back(number); });
  }

  for (size_t consumer = 0; consumer < num_consumers; ++consumer) {
    Go(&async_mutex, [&values, &result]() noexcept {
      result += values.back();
      values.pop_back();
    });
  }

  thread_pool.Wait();

  ASSERT_EQ(num_producers * number, result);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(AsyncMutex, Futures) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  AsyncMutex async_mutex(&thread_pool);

  int32_t account = 0;
  int32_t salary = 10;

  auto [f, p] = MakeContract<int32_t>();

  Go(&async_mutex, [p = p, salary, &account]() mutable noexcept {
    account += salary;
    std::move(p).SetValue(salary);
  });

  auto result = f.Via(&async_mutex)
                    .Then([&account](int32_t value) noexcept {
                      account -= 2;
                      return Ok(value * 2);
                    })
                    .Then([&account](int32_t value) noexcept {
                      account += 3;
                      return Ok(value * 4);
                    })
                    .Get();

  thread_pool.Wait();

  ASSERT_EQ(11, account);
  ASSERT_TRUE(result.HasValue());
  ASSERT_FALSE(result.HasError());
  ASSERT_EQ(80, result.Value());

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}
