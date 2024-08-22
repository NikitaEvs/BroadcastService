#pragma once

#include <atomic>
#include <cstddef>

template <typename T> class RefCounted {
public:
  // Must be called after initialization
  void Refer() { refs_.fetch_add(1); }

  void Derefer() {
    if (refs_.fetch_sub(1) - 1 == 0) {
      auto *ptr = static_cast<T *>(this);
      delete ptr;
    }
  }

  virtual ~RefCounted() = default;

private:
  std::atomic<size_t> refs_{0};
};
