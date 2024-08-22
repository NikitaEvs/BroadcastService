#pragma once

#include "concurrency/promise.hpp"
#include "concurrency/shared_state.hpp"
#include "concurrency/wait_group.hpp"
#include "runtime/just_execute.hpp"
#include "util/noncopyable.hpp"

#include <type_traits>
#include <variant>

// https://doc.rust-lang.org/std/primitive.unit.html
using Unit = std::monostate;

template <typename T> class Future : private NonCopyable {
public:
  using ValueType = T;
  using SharedStateT = SharedState<ValueType>;
  using SharedStatePtr = SharedStateT *;
  using Callback = typename SharedStateT::Callback;
  using Executor = typename SharedStateT::Executor;
  using ResultType = typename SharedStateT::ResultType;

  using WaitGroupT = WaitGroup;

  explicit Future(SharedStatePtr shared_state) : shared_state_(shared_state) {}

  Future(Future &&other) noexcept {
    shared_state_ = other.shared_state_;
    other.shared_state_ = nullptr;
  }

  void Consume(Callback callback) {
    assert(shared_state_ != nullptr);
    shared_state_->Consume(std::move(callback));
    shared_state_ = nullptr;
  }

  void SetExecutor(Executor executor) { shared_state_->SetExecutor(executor); }

  Executor GetExecutor() { return shared_state_->GetExecutor(); }

  ResultType Get() {
    SetExecutor(JustExecute());

    ResultType placeholder;
    WaitGroupT wg;

    wg.Add();
    Consume([&](ResultType result) {
      placeholder = std::move(result);
      wg.Done();
    });
    wg.Wait();

    return placeholder;
  }

  void Detach() {
    SetExecutor(JustExecute());
    Consume([](ResultType) noexcept {});
  }

  Future<T> Via(Executor executor) {
    SetExecutor(executor);
    return std::move(*this);
  }

  template <typename F> auto Then(F function) {
    auto [f, p] = MakeContract<GetType<std::invoke_result_t<F, ValueType>>>();
    f.SetExecutor(GetExecutor());

    Consume([function = std::move(function),
             p = std::move(p)](ResultType result) mutable {
      if (result.HasValue()) {
        std::move(p).Set(std::move(function(std::move(result.Value()))));
      } else {
        std::move(p).SetError(result.Error());
      }
    });

    return std::move(f);
  }

  template <typename F> auto Else(F function) {
    auto [f, p] = MakeContract<T>();
    f.SetExecutor(GetExecutor());

    Consume([function = std::move(function),
             p = std::move(p)](ResultType result) mutable {
      if (result.HasValue()) {
        std::move(p).Set(std::move(result));
      } else {
        std::move(p).Set(std::move(function(result.Error())));
      }
    });

    return std::move(f);
  }

  static Future<T> JustValue(T value) {
    auto [f, p] = MakeContract<T>();
    std::move(p).SetValue(std::move(value));
    return std::move(f);
  }

  static Future<T> JustErr(Error err) {
    auto [f, p] = MakeContract<T>();
    std::move(p).SetError(err);
    return std::move(f);
  }

private:
  SharedStatePtr shared_state_;

  // Code duplication for the sake of combinator's simplicity
  template <typename U>
  static std::tuple<Future<U>, Promise<U>> MakeContract() {
    auto *shared_state = new SharedState<U>();
    Future<U> future(shared_state);
    Promise<U> promise(shared_state);
    return std::make_tuple(std::move(future), std::move(promise));
  }
};
