#include "concurrency/fiber.hpp"

#include <cstddef>
#include <exception>

#include "runtime/executor.hpp"
#include "runtime/routine.hpp"

static thread_local Fiber *current_fiber = nullptr;

FiberPtr Fiber::CreateFiber(IExecutorPtr executor, Routine routine) {
  auto stack = Stack::AllocateStack(Fiber::kDefaultStackSize);
  return new Fiber(executor, std::move(routine), std::move(stack));
}

FiberPtr Fiber::Current() { return current_fiber; }

void Fiber::Schedule() { executor_->Execute(this); }

void Fiber::Suspend(ISuspendStrategyPtr suspender) {
  suspender_ = suspender;
  SwitchContext();
}

void Fiber::Run() {
  try {
    routine_();
  } catch (...) {
    exception_ = std::current_exception();
  }
  is_run_finished_ = true;
  context_.ExitTo(parent_context_);
}

void Fiber::Task() {
  Resume();
  if (!is_run_finished_) {
    if (suspender_ != nullptr) {
      suspender_->Suspend(this);
      suspender_ = nullptr;
    }
  } else {
    delete this;
  }
  current_fiber = nullptr;
}

IExecutorPtr Fiber::GetExecutor() { return executor_; }

Fiber::Fiber(IExecutorPtr executor, Routine routine, Stack stack)
    : executor_(executor), routine_(std::move(routine)),
      stack_(std::move(stack)), context_(stack_.MutableView(), this) {}

void Fiber::Resume() {
  FiberPtr previous_fiber_ = current_fiber;
  current_fiber = this;
  parent_context_.SwapTo(context_);
  current_fiber = previous_fiber_;

  if (exception_ != nullptr) {
    std::rethrow_exception(exception_);
  }
}

void Fiber::SwitchContext() { context_.SwapTo(parent_context_); }