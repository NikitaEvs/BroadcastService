#pragma once

#include "concurrency/shared_state.hpp"
#include "util/noncopyable.hpp"
#include "util/result.hpp"

template <typename T> class Promise {
public:
  using ValueType = T;
  using ResultType = Result<ValueType>;
  using SharedStateT = SharedState<ValueType>;
  using SharedStatePtr = SharedStateT *;

  explicit Promise(SharedStatePtr shared_state) : shared_state_(shared_state) {}

  // Hack to be able to use Promise in the std::function (which does not support
  // move-only lambdas). The best way to solve this issue is to use
  // fu2::unique_function
  Promise(const Promise &other) {
    shared_state_ = other.shared_state_;
    other.shared_state_ = nullptr;
  }
  Promise &operator=(const Promise &other) {
    shared_state_ = other.shared_state_;
    other.shared_state_ = nullptr;
    return *this;
  }

  Promise(Promise &&other) {
    shared_state_ = other.shared_state_;
    other.shared_state_ = nullptr;
  }

  Promise &operator=(Promise &&other) {
    shared_state_ = other.shared_state_;
    other.shared_state_ = nullptr;
    return *this;
  }

  void Set(ResultType result) && {
    if (shared_state_ != nullptr) {
      shared_state_->Produce(std::move(result));
      shared_state_ = nullptr;
    }
  }

  void SetValue(ValueType value) && {
    std::move(*this).Set(Ok(std::move(value)));
  }

  void SetError(std::error_code ec) && { std::move(*this).Set(Err<T>(ec)); }

private:
  // Hack to be able to use Promise in the std::function
  mutable SharedStatePtr shared_state_;
};
