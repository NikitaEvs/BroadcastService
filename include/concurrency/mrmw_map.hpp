#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

template <typename K, typename V, class Mutex> class MRMWUnorderedMap {
public:
  using Map = std::unordered_map<K, V>;
  using Node = typename Map::node_type;

  void Set(K key, V value) {
    std::lock_guard lock(mutex_);
    buffer_.emplace(std::move(key), std::move(value));
  }

  bool Has(K key) {
    std::lock_guard lock(mutex_);
    return buffer_.count(key) != 0;
  }

  std::optional<V> Take(K key) {
    Node node;
    {
      std::lock_guard lock(mutex_);
      node = buffer_.extract(key);
    }

    if (node.empty()) {
      return std::nullopt;
    } else {
      return {std::move(node.mapped())};
    }
  }

  std::optional<V> Get(K key) {
    std::lock_guard lock(mutex_);
    auto it = buffer_.find(key);
    if (it == buffer_.end()) {
      return std::nullopt;
    } else {
      return {it->second};
    }
  }

  std::vector<V> Drain() {
    std::lock_guard lock(mutex_);

    std::vector<V> values;
    values.reserve(buffer_.size());

    for (auto &&[_, v] : buffer_) {
      values.push_back(std::move(v));
    }

    return values;
  }

private:
  Mutex mutex_;
  Map buffer_;
};
