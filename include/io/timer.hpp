#pragma once

#include "util/instrusive_list.hpp"

#include <cstdint>
#include <functional>

class ITimer : public IntrusiveNode<ITimer> {
public:
  using Ticks = uint64_t;
  using TimerID = uint64_t;

  Ticks deadline{0};
  TimerID id{0};

  virtual ~ITimer() = default;

  virtual void Fire(TimerID id) = 0;
  virtual void Cancel() = 0;
};

using ITimerPtr = ITimer *;

class TimerFunction : public ITimer {
public:
  using Callback = std::function<void()>;

  void SetCallback(Callback callback) { callback_ = std::move(callback); }

  void Fire(TimerID) final {
    if (callback_) {
      callback_();
    }
  }

  void Cancel() final {
    // Nop
  }

private:
  Callback callback_;
};
