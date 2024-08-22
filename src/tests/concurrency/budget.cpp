#include <gtest/gtest.h>

#include "concurrency/budget.hpp"
#include "concurrency/wait_group.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"

TEST(SharedBudget, SmokeTest) {
  SharedBudget budget(100);
  ASSERT_EQ(50, budget.Credit(50));
}

TEST(SharedBudget, CappedBudget) {
  SharedBudget budget(100);
  ASSERT_EQ(100, budget.Credit(200));
}

TEST(SharedBudget, TopUp) {
  SharedBudget budget(100);
  budget.TopUp(100);
  ASSERT_EQ(200, budget.Credit(250));
}

TEST(SharedBudget, CapppedMax) {
  SharedBudget budget(100, 150);
  budget.TopUp(100);
  ASSERT_EQ(150, budget.Credit(200));
}

TEST(SharedBudget, Concurrency) {
  constexpr size_t num_producers = 3;
  constexpr size_t num_consumers = num_producers;
  constexpr size_t num_produced = 1000000;

  ThreadPool thread_pool(6);
  thread_pool.Start();
  SharedBudget budget(0, num_produced * num_producers);

  for (size_t producer = 0; producer < num_producers; ++producer) {
    thread_pool.Execute(Lambda::Create([&budget]() {
      for (size_t produced = 0; produced < num_produced; ++produced) {
        budget.TopUp(1);
      }
    }));
  }

  std::atomic<size_t> counter{0};

  for (size_t consumer = 0; consumer < num_consumers; ++consumer) {
    thread_pool.Execute(Lambda::Create([&budget, &counter]() {
      SharedBudget::CounterT sum_credited = 0;
      while (sum_credited < static_cast<SharedBudget::CounterT>(num_produced)) {
        auto credited = budget.Credit(1);
        if (credited > 0) {
          counter.fetch_add(credited);
          sum_credited += credited;
        }
      }
    }));
  }

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();

  ASSERT_EQ(num_produced * num_producers, counter.load());
}