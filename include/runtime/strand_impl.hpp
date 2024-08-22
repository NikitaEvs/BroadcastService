#pragma once

// Inspired from
// https://github.com/facebook/folly/blob/main/folly/executors/StrandExecutor.h
// Can not be used separately, must be used with a proper wrapper

#include "concurrency/refcounted.hpp"
#include "concurrency/spinlock.hpp"
#include "concurrency/task_queue.hpp"
#include "runtime/executor.hpp"
#include "runtime/task.hpp"
#include "util/logging.hpp"
#include "util/nonmovable.hpp"
#include <string>

template <typename T>
class IStrandImpl : public ITask,
                    public RefCounted<IStrandImpl<T>>,
                    public NonMoveable {
public:
  using TaskT = T;
  using TaskTPtr = TaskT *;
  using BatchedLFQueueT = BatchedLFQueue<TaskT>;
  using List = typename BatchedLFQueueT::List;

  explicit IStrandImpl(IExecutorPtr underylying) : underlying_(underylying) {}
  virtual ~IStrandImpl() = default;

  void Process(TaskTPtr task) {
    tasks_.Push(task);

    // mutex_.Lock();
    // tasks_.PushBack(task);
    // mutex_.Unlock();

    if (submitted_.fetch_add(1) == 0) {
      this->Refer();
      underlying_->Execute(this);
    }
  }

  void Task() override {
    auto batch = tasks_.TryPopAll();

    // mutex_.Lock();
    // auto batch = std::move(tasks_);
    // mutex_.Unlock();

    // LOG_ERROR("RELICH", "Strand batch size " + std::to_string(batch.Size()));

    if (batch.Empty()) {
      return;
    }

    auto num_tasks = ProcessBatch(std::move(batch));

    if (submitted_.fetch_sub(num_tasks) > num_tasks) {
      // Resubmit yourself in case there are additional tasks
      this->Refer();
      underlying_->Execute(this);
    }

    this->Derefer();
  }

  virtual int64_t ProcessBatch(List batch) = 0;

private:
  SpinLock mutex_;
  BatchedLFQueueT tasks_;
  // List tasks_;
  std::atomic<int64_t> submitted_{0};
  IExecutorPtr underlying_;
};
