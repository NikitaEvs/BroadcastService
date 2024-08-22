#pragma once

#include <mutex>
#include <optional>
#include <unordered_set>

template <typename K, class Mutex> class MRMWUnorderedSet {
public:
  using Set = std::unordered_set<K>;
  using SizeT = typename Set::size_type;

  bool Insert(K key) {
    std::lock_guard lock(mutex_);
    auto [_, not_exists] = buffer_.insert(std::move(key));
    return not_exists;
  }

  bool Has(const K &key) {
    std::lock_guard lock(mutex_);
    return buffer_.count(key) != 0;
  }

  void Delete(const K &key) {
    std::lock_guard lock(mutex_);
    buffer_.erase(key);
  }

  SizeT EstimateSize() {
    std::lock_guard lock(mutex_);
    return buffer_.size();
  }

private:
  Mutex mutex_;
  Set buffer_;
};
