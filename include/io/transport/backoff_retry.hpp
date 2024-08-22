#pragma once

#include <atomic>
#include <functional>

#include "io/timer.hpp"
#include "io/timer_service.hpp"
#include "util/random_util.hpp"
#include "util/statistics.hpp"

class RetryPolicy {
public:
  using DurationMs = TimerService::DurationMs;

  RetryPolicy(DurationMs timeout_ms, double multiplier,
              DurationMs max_timeout_ms, bool with_jitter = false)
      : timeout_ms_(timeout_ms), multiplier_(multiplier),
        max_timeout_ms_(max_timeout_ms), initial_timeout_(timeout_ms),
        with_jitter_(with_jitter) {}

  RetryPolicy(const RetryPolicy &other) {
    timeout_ms_ = other.timeout_ms_;
    multiplier_ = other.multiplier_;
    max_timeout_ms_ = other.max_timeout_ms_;
    initial_timeout_ = other.initial_timeout_;
    with_jitter_ = other.with_jitter_;
  }

  RetryPolicy &operator=(const RetryPolicy &other) {
    timeout_ms_ = other.timeout_ms_;
    multiplier_ = other.multiplier_;
    max_timeout_ms_ = other.max_timeout_ms_;
    initial_timeout_ = other.initial_timeout_;
    with_jitter_ = other.with_jitter_;
    return *this;
  }

  DurationMs GetTimeout() const { return timeout_ms_; }

  bool IsValid() const { return timeout_ms_ <= max_timeout_ms_; }

  void Next() {
    auto max_new_timeout =
        static_cast<DurationMs>(static_cast<double>(timeout_ms_) * multiplier_);
    if (with_jitter_) {
      timeout_ms_ = RandInt(timeout_ms_, max_new_timeout);
    } else {
      timeout_ms_ = max_new_timeout;
    }
  }

  void Reset() { timeout_ms_ = initial_timeout_; }

private:
  DurationMs timeout_ms_;
  double multiplier_;
  DurationMs max_timeout_ms_;
  DurationMs initial_timeout_;
  bool with_jitter_;
};

// The main difference from the IRetryableHandler is that we preserve the same
// timer id here
class BackoffRetryTimer : public ITimer {
public:
  using DurationMs = TimerService::DurationMs;
  using Callback = std::function<void()>;

  static BackoffRetryTimer *Create(TimerService &timer_service,
                                   Callback callback, RetryPolicy policy) {
    Statistics::Instance().Change("backoff_retries", 1);
    return new BackoffRetryTimer(timer_service, std::move(callback),
                                 std::move(policy));
  }

  void Fire(TimerID id) final {
    Retry();
    retry_policy_.Next();
    Rearm(id, retry_policy_);
    Statistics::Instance().Change("backoff_deletions", 1);
    delete this;
  }

  void Cancel() final {
    Statistics::Instance().Change("backoff_deletions", 1);
    delete this;
  }

private:
  TimerService &timer_service_;
  Callback callback_;
  RetryPolicy retry_policy_;

  BackoffRetryTimer(TimerService &timer_service, Callback callback,
                    RetryPolicy retry_policy)
      : timer_service_(timer_service), callback_(std::move(callback)),
        retry_policy_(std::move(retry_policy)) {}

  void Retry() { callback_(); }

  void Rearm(TimerID id, const RetryPolicy &policy) {
    if (policy.IsValid()) {
      auto timer = Create(timer_service_, std::move(callback_), policy);
      timer_service_.AddTimer(timer, id, policy.GetTimeout());
    }
  }
};
