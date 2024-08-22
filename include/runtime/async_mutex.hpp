#pragma once

// Asynchoronous fiber mutex

#include "runtime/executor.hpp"
#include "runtime/task.hpp"
#include "util/nonmovable.hpp"

class AsyncMutexImpl;

class AsyncMutex : public ITask, public IExecutor, private NonMoveable {
public:
  using Impl = AsyncMutexImpl;
  using ImplPtr = AsyncMutexImpl *;

  explicit AsyncMutex(IExecutorPtr underlying);
  ~AsyncMutex();

  void Execute(ITaskPtr task) override;

  void Task() override;

private:
  ImplPtr impl_;
};
