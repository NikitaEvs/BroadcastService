#pragma once

#include "concurrency/contract.hpp"
#include "concurrency/future.hpp"
#include "concurrency/promise.hpp"

template <typename F>
Future<GetType<std::invoke_result_t<F>>> Async(IExecutorPtr executor,
                                               F function) {
  auto [f, p] = MakeContract<GetType<std::invoke_result_t<F>>>();
  executor->Execute(Lambda::Create(
      [function = std::move(function), p = std::move(p)]() mutable {
        std::move(p).Set(std::move(function()));
      }));
  return std::move(f);
}
