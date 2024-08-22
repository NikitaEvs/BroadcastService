#include "runtime/just_execute.hpp"

#include "runtime/executor.hpp"
#include "runtime/task.hpp"

class JustExecutor : public IExecutor {
public:
  void Execute(ITaskPtr task) { task->Task(); }
};

IExecutorPtr JustExecute() {
  static JustExecutor executor;
  return &executor;
}
