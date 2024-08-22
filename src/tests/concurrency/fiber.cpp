#include "concurrency/fiber.hpp"

#include "runtime/api.hpp"
#include "runtime/executor.hpp"
#include "runtime/runnable.hpp"
#include "runtime/suspend_strategy.hpp"
#include "runtime/test_executor.hpp"

#include <cstdint>
#include <gtest/gtest.h>

TEST(Fiber, SmokeTest) {
  TestExecutor executor;

  bool flag = false;

  auto *fiber =
      Fiber::CreateFiber(&executor, [&flag]() noexcept { flag = true; });
  fiber->Schedule();

  ASSERT_FALSE(flag);

  executor.Step();

  ASSERT_TRUE(flag);
  ASSERT_FALSE(executor.HasStep());
}

TEST(Fiber, Go) {
  TestExecutor executor;

  bool flag = false;
  Go(&executor, [&flag]() noexcept { flag = true; });

  ASSERT_FALSE(flag);

  executor.Step();

  ASSERT_TRUE(flag);
  ASSERT_FALSE(executor.HasStep());
}

class CorouitineSuspendStrategy : public ISuspendStrategy {
public:
  void Suspend(Fiber *) final {
    // Do nothing
  }
};

TEST(Fiber, Suspend) {
  TestExecutor executor;

  int32_t number = 0;
  auto counter = [&number]() mutable {
    CorouitineSuspendStrategy co_strategy;
    ++number;
    Suspend(&co_strategy);
    ++number;
    Suspend(&co_strategy);
  };

  auto *fiber = Fiber::CreateFiber(&executor, std::move(counter));
  fiber->Schedule();

  ASSERT_EQ(0, number);
  ASSERT_TRUE(executor.HasStep());

  executor.Step();

  ASSERT_EQ(1, number);
  ASSERT_FALSE(executor.HasStep());

  fiber->Schedule();

  ASSERT_TRUE(executor.HasStep());

  executor.Step();

  ASSERT_EQ(2, number);
  ASSERT_FALSE(executor.HasStep());

  fiber->Schedule();

  ASSERT_TRUE(executor.HasStep());

  executor.Step();

  ASSERT_FALSE(executor.HasStep());
}

class Range {
public:
  explicit Range(IExecutorPtr exe, int32_t from, int32_t to)
      : current_(from), to_(to) {
    Go(exe, [this]() {
      while (true) {
        ++current_;
        if (current_ == to_) {
          return;
        } else {
          Yield();
        }
      }
    });
  }

  bool Next() {
    if (current_ == to_) {
      return false;
    }
    Yield();
    return true;
  }

  int32_t Current() const { return current_; }

private:
  int32_t current_;
  int32_t from_;
  int32_t to_;
};

TEST(Fiber, Range) {
  TestExecutor executor;

  Go(&executor, [&executor]() {
    Range range(&executor, 1, 5);

    int32_t expected = 1;
    int32_t current = range.Current();
    while (true) {
      ASSERT_EQ(expected, current);
      if (!range.Next()) {
        break;
      }
      current = range.Current();
      ++expected;
    }
  });

  ASSERT_TRUE(executor.HasStep());
  while (executor.HasStep()) {
    executor.Step();
  }
  ASSERT_FALSE(executor.HasStep());
}
