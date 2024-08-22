#include "runtime/async_mutex.hpp"

#include "runtime/strand_impl.hpp"

class AsyncMutexImpl : public IStrandImpl<ITask> {
public:
  explicit AsyncMutexImpl(IExecutorPtr underylying)
      : IStrandImpl(underylying) {}

  int64_t ProcessBatch(List batch) override {
    int64_t num_tasks = 0;
    while (auto *task = batch.PopFront()) {
      ++num_tasks;
      task->Task();
    }
    return num_tasks;
  }
};

AsyncMutex::AsyncMutex(IExecutorPtr underlying)
    : impl_(new AsyncMutexImpl(underlying)) {
  impl_->Refer();
}

AsyncMutex::~AsyncMutex() { impl_->Derefer(); }

void AsyncMutex::Execute(ITaskPtr task) { impl_->Process(task); }

void AsyncMutex::Task() { impl_->Task(); }
