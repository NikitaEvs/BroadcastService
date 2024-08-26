#include <gtest/gtest.h>
#include <iterator>
#include <thread>

#include "concurrency/fiber.hpp"
#include "concurrency/rendezvous.hpp"
#include "io/fiber_sleeper.hpp"
#include "io/timer.hpp"
#include "io/timer_service.hpp"
#include "runtime/api.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/suspend_strategy.hpp"
#include "runtime/thread_pool.hpp"

using namespace std::chrono_literals;

enum class Flag {
  kNone,
  kFired,
  kCancelled,
};

class FlagTimer : public ITimer {
public:
  static FlagTimer *Create(std::atomic<Flag> &flag) {
    return new FlagTimer(flag);
  }

  void Fire(TimerID) final {
    flag_.store(Flag::kFired);
    delete this;
  }

  void Cancel() final {
    flag_.store(Flag::kCancelled);
    delete this;
  }

private:
  std::atomic<Flag> &flag_;

  FlagTimer(std::atomic<Flag> &flag) : flag_(flag) {}
};

TEST(TimerService, ShadowTest) {
  EventLoop event_loop(100);
  event_loop.Setup();

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  ThreadPool thread_pool(5);
  thread_pool.Start();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  std::atomic<Flag> flag = Flag::kNone;
  auto *timer = FlagTimer::Create(flag);
  timer_service.AddTimer(timer, 100);

  std::this_thread::sleep_for(500ms);

  ASSERT_EQ(Flag::kFired, flag.load());

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(TimerService, Cancel) {
  EventLoop event_loop(100);
  event_loop.Setup();

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  ThreadPool thread_pool(5);
  thread_pool.Start();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  std::atomic<Flag> flag = Flag::kNone;
  auto *timer = FlagTimer::Create(flag);
  auto id = timer_service.AddTimer(timer, 100);

  std::this_thread::sleep_for(50ms);

  timer_service.CancelTimer(id);

  std::this_thread::sleep_for(500ms);

  ASSERT_EQ(Flag::kCancelled, flag.load());

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(TimerService, MulitpleFire) {
  EventLoop event_loop(100);
  event_loop.Setup();

  TimerService timer_service(1);
  timer_service.Register(event_loop);

  ThreadPool thread_pool(5);
  thread_pool.Start();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  constexpr size_t num_timers = 1000;
  std::vector<std::atomic<Flag>> flags(num_timers);
  std::vector<FlagTimer *> timers(num_timers);
  std::vector<ITimer::TimerID> ids(num_timers);

  for (size_t timer_idx = 0; timer_idx < num_timers; ++timer_idx) {
    timers[timer_idx] = FlagTimer::Create(flags[timer_idx]);
    ids[timer_idx] =
        timer_service.AddTimer(timers[timer_idx], 100 * (1 + timer_idx % 3));
  }

  std::this_thread::sleep_for(500ms);

  for (auto &flag : flags) {
    ASSERT_EQ(Flag::kFired, flag.load());
  }

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(TimerService, MulitpleFireMultipleCancel) {
  EventLoop event_loop(100);
  event_loop.Setup();

  TimerService timer_service(10);
  timer_service.Register(event_loop);

  ThreadPool thread_pool(5);
  thread_pool.Start();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  constexpr size_t num_timers = 1000;
  std::vector<std::atomic<Flag>> flags(num_timers);
  std::vector<FlagTimer *> timers(num_timers);
  std::vector<ITimer::TimerID> ids(num_timers);

  for (size_t timer_idx = 0; timer_idx < num_timers; ++timer_idx) {
    timers[timer_idx] = FlagTimer::Create(flags[timer_idx]);
    ids[timer_idx] =
        timer_service.AddTimer(timers[timer_idx], 100 * (1 + timer_idx % 2));
  }

  for (size_t timer_idx = 0; timer_idx < num_timers; ++timer_idx) {
    if (timer_idx % 2 == 1) {
      timer_service.CancelTimer(ids[timer_idx]);
    }
  }

  std::this_thread::sleep_for(500ms);

  for (size_t flag_idx = 0; flag_idx < num_timers; ++flag_idx) {
    auto &flag = flags[flag_idx];
    if (flag_idx % 2 == 1) {
      ASSERT_EQ(Flag::kCancelled, flag.load());
    } else {
      ASSERT_EQ(Flag::kFired, flag.load());
    }
  }

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

class FiberTimer : public ITimer {
public:
  static FiberTimer *Create(FiberPtr fiber) { return new FiberTimer(fiber); }

  void Fire(TimerID) override {
    fiber_->Schedule();
    delete this;
  }

  void Cancel() override {
    delete fiber_;
    delete this;
  }

private:
  FiberPtr fiber_;

  explicit FiberTimer(FiberPtr fiber) : fiber_(fiber) {}
};
