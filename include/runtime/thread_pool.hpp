#pragma once

#include <thread>
#include <vector>

#include "concurrency/queue.hpp"
#include "concurrency/wait_group.hpp"
#include "runtime/executor.hpp"
#include "runtime/task.hpp"

class ThreadPool : public IExecutor {
public:
  explicit ThreadPool(size_t num_workers);
  ~ThreadPool();

  void Start();

  void Wait();
  void SignalStop();
  void WaitForStop();
  // Possible memory leak, call only to stop everything without care
  void Shutdown();

  void Execute(ITaskPtr task) final;

  static IExecutorPtr Current();

private:
  size_t num_workers_;
  std::vector<std::thread> workers_;

  UnboundedMRMWQueue<ITaskPtr> tasks_;
  WaitGroup pending_tasks_;

  void Routine();
};
