#pragma once

#include "concurrency/spinlock.hpp"
#include "util/statistics.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

#include <mutex>

class SharedBudget {
public:
  using CounterT = size_t;
  using Mutex = SpinLock;

  explicit SharedBudget(CounterT initial_budget)
      : SharedBudget(initial_budget, std::numeric_limits<CounterT>::max()) {}

  SharedBudget(CounterT initial_budget, CounterT max_budget)
      : counter_(initial_budget), max_budget_(max_budget) {
    Statistics::Instance().Change(
        "send_budget", static_cast<Statistics::Type>(initial_budget));
  }

  CounterT Credit() { return Credit(std::numeric_limits<CounterT>::max()); }

  CounterT Credit(CounterT max_credit) {
    std::lock_guard lock(mutex_);
    auto credit = std::min(counter_, max_credit);
    counter_ -= credit;
    Statistics::Instance().Change("send_budget",
                                  -static_cast<Statistics::Type>(credit));
    return credit;
  }

  void TopUp(CounterT credit) {
    std::lock_guard lock(mutex_);
    Statistics::Instance().Change(
        "send_budget", static_cast<Statistics::Type>(
                           std::min(max_budget_ - counter_, credit)));
    counter_ = std::min(counter_ + credit, max_budget_);
  }

private:
  CounterT counter_;
  const CounterT max_budget_;
  Mutex mutex_;
};