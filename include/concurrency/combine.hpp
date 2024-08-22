#pragma once

#include "concurrency/contract.hpp"
#include "concurrency/future.hpp"
#include "concurrency/promise.hpp"
#include "concurrency/spinlock.hpp"

#include <atomic>
#include <memory>
#include <mutex>

template <template <typename> class Combinator, typename T>
auto Combine(std::vector<Future<T>> inputs) {
  auto [f, p] = MakeContract<typename Combinator<T>::ResultType>();

  auto collector_ptr =
      std::make_shared<Combinator<T>>(inputs.size(), std::move(p));

  for (auto &input_future : inputs) {
    input_future.Consume([collector_ptr](Result<T> result) {
      if (result.HasValue()) {
        collector_ptr->SetValue(std::move(result.Value()));
      } else {
        collector_ptr->SetError(result.Error());
      }
    });
  }

  return std::move(f);
}

template <typename T> struct FirstCombinator {
  using ResultType = T;

  explicit FirstCombinator(size_t size, Promise<T> promise)
      : counter(size), promise(std::move(promise)) {}

  void SetValue(T value) {
    if (!is_completed.exchange(true)) {
      std::move(promise).SetValue(std::move(value));
    }
  }

  void SetError(std::error_code ec) {
    if ((counter.fetch_sub(1) == 1) && !is_completed.exchange(true)) {
      std::move(promise).SetError(ec);
    }
  }

  std::atomic<size_t> counter;
  std::atomic<bool> is_completed{false};
  Promise<T> promise;
};

template <typename T> struct AllCombinator {
  using ResultType = std::vector<T>;
  using Mutex = SpinLock;

  explicit AllCombinator(size_t size, Promise<std::vector<T>> promise)
      : futures_number(size), promise(std::move(promise)) {}

  void SetValue(T value) {
    std::lock_guard lock(mutex);
    results.push_back(std::move(value));

    if (results.size() == futures_number) {
      std::move(promise).SetValue(std::move(results));
    }
  }

  void SetError(std::error_code ec) { std::move(promise).SetError(ec); }

  Mutex mutex;
  std::vector<T> results; // guarded by mutex
  size_t futures_number;
  Promise<std::vector<T>> promise;
};

template <typename T> Future<T> First(std::vector<Future<T>> inputs) {
  assert(!inputs.empty());
  return std::move(Combine<FirstCombinator, T>(std::move(inputs)));
}

template <typename T>
Future<std::vector<T>> All(std::vector<Future<T>> inputs) {
  assert(!inputs.empty());
  return std::move(Combine<AllCombinator, T>(std::move(inputs)));
}
