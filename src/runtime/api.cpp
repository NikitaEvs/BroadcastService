#include "runtime/api.hpp"

#include "concurrency/fiber.hpp"
#include "runtime/executor.hpp"
#include "runtime/suspend_strategy.hpp"

#include <cassert>

class YieldSuspendStrategy : public ISuspendStrategy {
public:
  void Suspend(Fiber *fiber) final { fiber->Schedule(); }
};

void Go(IExecutorPtr executor, Routine routine) {
  auto *fiber = Fiber::CreateFiber(executor, std::move(routine));
  fiber->Schedule();
}

void Go(Routine routine) {
  auto current_fiber = Fiber::Current();
  assert(current_fiber != nullptr &&
         "Go without executor must be called only from the fiber itself");
  auto *fiber =
      Fiber::CreateFiber(current_fiber->GetExecutor(), std::move(routine));
  fiber->Schedule();
}

void Suspend(ISuspendStrategyPtr suspend_strategy) {
  auto *current_fiber = Fiber::Current();
  assert(current_fiber != nullptr &&
         "Suspend is called outside of the fiber context");
  current_fiber->Suspend(suspend_strategy);
}

void Yield() {
  auto *current_fiber = Fiber::Current();
  assert(current_fiber != nullptr &&
         "Yield is called outside of the fiber context");

  // Stack will be preserved in the fiber until it will be resumed
  YieldSuspendStrategy strategy;
  current_fiber->Suspend(&strategy);
}
