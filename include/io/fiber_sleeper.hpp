#pragma once

#include <cassert>

#include "concurrency/fiber.hpp"
#include "concurrency/rendezvous.hpp"
#include "io/timer.hpp"
#include "runtime/suspend_strategy.hpp"

class FiberSleeper : public ITimer, public ISuspendStrategy {
public:
  void Suspend(Fiber *fiber) override {
    fiber_ = fiber;
    if (rendezvous_.Producer()) {
      Reschedule();
    }
  }

  void Fire(TimerID) override {
    if (rendezvous_.Consumer()) {
      Reschedule();
    }
  }

  void Cancel() override {
    // Nop
  }

private:
  Fiber *fiber_;
  Rendezvous rendezvous_;

  void Reschedule() {
    assert(fiber_ != nullptr);
    fiber_->Schedule();
  }
};
