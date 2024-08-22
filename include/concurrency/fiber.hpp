#pragma once

#include "concurrency/context.hpp"
#include "runtime/executor.hpp"
#include "runtime/routine.hpp"
#include "runtime/runnable.hpp"
#include "runtime/suspend_strategy.hpp"

#include <exception>

class Fiber;
using FiberPtr = Fiber *;

class Fiber final : public IRunnable, ITask {
public:
  static FiberPtr CreateFiber(IExecutorPtr executor, Routine routine);

  static FiberPtr Current();

  void Schedule();

  void Suspend(ISuspendStrategyPtr suspender);

  void Run() final;

  void Task() final;

  IExecutorPtr GetExecutor();

private:
  static constexpr size_t kDefaultStackSize = 64 * 4096;

  IExecutorPtr executor_;
  Routine routine_;
  Stack stack_;

  FiberContext context_;
  FiberContext parent_context_;
  ISuspendStrategyPtr suspender_;

  std::exception_ptr exception_;
  bool is_run_finished_{false};

  Fiber(IExecutorPtr executor, Routine routine, Stack stack);

  void Resume();
  void SwitchContext();
};