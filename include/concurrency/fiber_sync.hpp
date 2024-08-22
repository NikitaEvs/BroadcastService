#pragma once

#include <cassert>
#include <utility>

#include "concurrency/fiber.hpp"
#include "concurrency/spinlock.hpp"
#include "runtime/api.hpp"
#include "runtime/suspend_strategy.hpp"
#include "util/instrusive_list.hpp"

class SleepQueueSuspendStrategy
    : public ISuspendStrategy,
      public IntrusiveNode<SleepQueueSuspendStrategy> {
public:
  using Mutex = SpinLock;

  explicit SleepQueueSuspendStrategy(Mutex &mutex) : mutex_(mutex) {}

  void Suspend(Fiber *fiber) {
    fiber_ = fiber;
    mutex_.Unlock();
  }

  void Awake() {
    assert(fiber_ != nullptr &&
           "Fiber is nullptr in the SleepQueueSuspendStrategy");
    fiber_->Schedule();
  }

private:
  Mutex &mutex_;
  Fiber *fiber_{nullptr};
};

class FiberOneShotEvent {
public:
  using List = IntrusiveList<SleepQueueSuspendStrategy>;
  using Mutex = SpinLock;

  void Wait() {
    mutex_.Lock();
    // strategy is preserved on the fiber's stack
    SleepQueueSuspendStrategy strategy(mutex_);
    if (is_fired_) {
      mutex_.Unlock();
      return;
    }
    sleepers_.PushBack(&strategy);
    Suspend(&strategy);
  }

  void Fire() {
    mutex_.Lock();
    List sleepers(std::move(sleepers_));
    is_fired_ = true;
    mutex_.Unlock();

    while (auto *sleeper = sleepers.PopFront()) {
      sleeper->Awake();
    }
  }

private:
  Mutex mutex_;
  List sleepers_;        // guarded by mutex_
  bool is_fired_{false}; // guarded by mutex_
};

class FiberMutex {
public:
  using List = IntrusiveList<SleepQueueSuspendStrategy>;
  using Mutex = SpinLock;

  void Lock() {
    mutex_.Lock();
    if (std::exchange(is_locked_, true)) {
      // strategy is preserved on the fiber's stack
      SleepQueueSuspendStrategy strategy(mutex_);
      sleepers_.PushBack(&strategy);
      Suspend(&strategy);
    } else {
      mutex_.Unlock();
    }
  }

  void Unlock() {
    mutex_.Lock();
    if (auto *sleeper = sleepers_.PopFront()) {
      mutex_.Unlock();
      sleeper->Awake();
    } else {
      is_locked_ = false;
      mutex_.Unlock();
    }
  }

  void lock() { Lock(); }

  void unlock() { Unlock(); }

private:
  Mutex mutex_;
  List sleepers_;         // guarded by mutex_
  bool is_locked_{false}; // guarded by mutex_
};

class FiberWaitGroup {
public:
  void Add(size_t count = 1) { counter_.fetch_add(count); }

  void Done(size_t count = 1) {
    if (counter_.fetch_sub(count) == 1) {
      event_.Fire();
    }
  }

  void Wait() { event_.Wait(); }

private:
  std::atomic<size_t> counter_{0};
  FiberOneShotEvent event_;
};
