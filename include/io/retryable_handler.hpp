#pragma once

#include "io/timer.hpp"
#include "io/timer_service.hpp"
#include "io/transport/backoff_retry.hpp"
#include "runtime/event_loop.hpp"

class IRetryableHandler : public IHandler {
  using RetryCallback = TimerFunction::Callback;
  using DurationMs = TimerService::DurationMs;

public:
  explicit IRetryableHandler(TimerService &timer_service)
      : timer_service_(timer_service) {}

  void SetEvent(const Event &event) { event_ = event; }

protected:
  Event event_;
  TimerFunction retry_function_;
  TimerService &timer_service_;

  bool Retry(EventLoop &event_loop, RetryPolicy &policy) {
    if (policy.IsValid()) {
      auto timeout = policy.GetTimeout();
      policy.Next();
      ScheduleAfter(event_loop, timeout);
      return true;
    } else {
      return false;
    }
  }

  void ScheduleAfter(EventLoop &event_loop, DurationMs timeout) {
    retry_function_.SetCallback(GetRetryCallback(event_loop));
    timer_service_.AddTimer(&retry_function_, timeout);
  }

  RetryCallback GetRetryCallback(EventLoop &event_loop) {
    return [this, &event_loop]() { Rearm(event_loop); };
  }

  void Rearm(EventLoop &event_loop) { event_loop.ModEvent(event_); }
};