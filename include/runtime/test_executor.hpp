#pragma once

#include "runtime/executor.hpp"
#include "runtime/task.hpp"

#include <mutex>
#include <queue>

class TestExecutor : public IExecutor {
public:
  void Execute(ITaskPtr task) final {
    std::lock_guard lock(mutex_);
    tasks_.push(task);
  }

  void Step() {
    ITaskPtr task = nullptr;

    {
      std::lock_guard lock(mutex_);
      if (HasStep(lock)) {
        task = tasks_.front();
        tasks_.pop();
      }
    }

    if (task != nullptr) {
      task->Task();
    }
  }

  bool HasStep() const {
    std::lock_guard lock(mutex_);
    return HasStep(lock);
  }

private:
  mutable std::mutex mutex_;
  std::queue<ITaskPtr> tasks_;

  bool HasStep(std::lock_guard<std::mutex> &) const { return !tasks_.empty(); }
};