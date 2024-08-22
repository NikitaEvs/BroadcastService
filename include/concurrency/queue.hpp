#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

template <typename T> class UnboundedMRMWQueue {
public:
  bool Put(T value) {
    std::lock_guard lock(mutex_);
    if (is_closed_) {
      return false;
    }
    buffer_.push_back(std::move(value));
    not_empty_or_closed_.notify_one();
    return true;
  }

  std::optional<T> Take() {
    std::unique_lock lock(mutex_);
    while (buffer_.empty() && !is_closed_) {
      not_empty_or_closed_.wait(lock);
    }
    if (buffer_.empty()) {
      return std::nullopt;
    }
    auto value = std::move(buffer_.front());
    buffer_.pop_front();
    return {std::move(value)};
  }

  void Close() {
    std::lock_guard lock(mutex_);
    is_closed_ = true;
    not_empty_or_closed_.notify_all();
  }

  void Clear() {
    std::lock_guard lock(mutex_);
    is_closed_ = true;
    buffer_.clear();
    not_empty_or_closed_.notify_all();
  }

private:
  std::mutex mutex_;
  std::deque<T> buffer_;  // guarded by mutex_
  bool is_closed_{false}; // guarded by mutex_
  std::condition_variable not_empty_or_closed_;
};
