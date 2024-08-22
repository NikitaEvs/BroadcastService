#pragma once

#include "concurrency/future.hpp"
#include "concurrency/promise.hpp"
#include "concurrency/shared_state.hpp"
#include "runtime/executor.hpp"

#include <tuple>

template <typename T> std::tuple<Future<T>, Promise<T>> MakeContract() {
  auto *shared_state = new SharedState<T>();
  Future<T> future(shared_state);
  Promise<T> promise(shared_state);
  return std::make_tuple(std::move(future), std::move(promise));
}
