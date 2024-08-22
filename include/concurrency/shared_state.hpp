#pragma once

#include <cassert>
#include <functional>
#include <optional>

#include "concurrency/rendezvous.hpp"
#include "runtime/executor.hpp"
#include "runtime/just_execute.hpp"
#include "runtime/task.hpp"
#include "util/noncopyable.hpp"
#include "util/result.hpp"

template <typename T> class SharedState : private NonCopyable {
public:
  using ResultType = Result<T>;
  using Callback = std::function<void(ResultType)>;
  using Executor = IExecutorPtr;

  SharedState() : executor_(JustExecute()) {}

  Executor GetExecutor() { return executor_; }

  void SetExecutor(Executor executor) { executor_ = executor; }

  void Produce(ResultType result) {
    result_placeholder_ = std::move(result);
    if (rendezvous_.Producer()) {
      DoCallback();
    }
  }

  void Consume(Callback callback) {
    callback_ = std::move(callback);
    if (rendezvous_.Consumer()) {
      DoCallback();
    }
  }

private:
  ResultType result_placeholder_;
  Rendezvous rendezvous_;
  Callback callback_;
  Executor executor_;

  void DoCallback() {
    assert(executor_ != nullptr);
    executor_->Execute(
        Lambda::Create([callback = std::move(callback_),
                        result = std::move(result_placeholder_)]() {
          callback(std::move(result));
        }));
    delete this;
  }
};
