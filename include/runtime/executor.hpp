#pragma once

#include "runtime/task.hpp"

#include <functional>

class IExecutor {
public:
  virtual ~IExecutor() = default;

  virtual void Execute(ITaskPtr task) = 0;
};

using IExecutorPtr = IExecutor *;
