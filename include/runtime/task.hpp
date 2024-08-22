#pragma once

#include "runtime/routine.hpp"
#include "util/instrusive_list.hpp"

class ITaskBase {
public:
  virtual ~ITaskBase() = default;
  virtual void Task() = 0;
};

using ITaskBasePtr = ITaskBase *;

class ITask : public ITaskBase, public IntrusiveNode<ITask> {};

using ITaskPtr = ITask *;

class Lambda : public ITask {
public:
  static ITaskPtr Create(Routine routine) {
    auto *lambda = new Lambda(std::move(routine));
    return static_cast<ITaskPtr>(lambda);
  }

  void Task() final {
    routine_();
    delete this;
  }

private:
  explicit Lambda(Routine routine) : routine_(std::move(routine)) {}

  Routine routine_;
};
