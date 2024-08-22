#include "runtime/thread_pool.hpp"

#include <cassert>

static thread_local IExecutorPtr current_threadpool = nullptr;

ThreadPool::ThreadPool(size_t num_workers) : num_workers_(num_workers) {}

ThreadPool::~ThreadPool() { assert(workers_.empty()); }

void ThreadPool::Start() {
  workers_.reserve(num_workers_);
  for (size_t worker_idx = 0; worker_idx < num_workers_; ++worker_idx) {
    workers_.emplace_back([this]() { Routine(); });
  }
}

void ThreadPool::Wait() { pending_tasks_.Wait(); }

void ThreadPool::SignalStop() { tasks_.Close(); }

void ThreadPool::WaitForStop() {
  Wait();
  for (auto &worker : workers_) {
    worker.join();
  }
  workers_.clear();
}

void ThreadPool::Shutdown() {
  tasks_.Clear();
  for (auto &worker : workers_) {
    worker.join();
  }
  workers_.clear();
}

void ThreadPool::Execute(ITaskPtr task) {
  pending_tasks_.Add();
  if (!tasks_.Put(task)) {
    pending_tasks_.Done();
  }
}

IExecutorPtr ThreadPool::Current() { return current_threadpool; }

void ThreadPool::Routine() {
  current_threadpool = this;
  while (auto task = tasks_.Take()) {
    task.value()->Task();
    pending_tasks_.Done();
  }
  current_threadpool = nullptr;
}
