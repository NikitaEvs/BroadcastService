#pragma once

#include <atomic>

enum class UniqueClass { kMessageID };

template <UniqueClass type, typename IDType> class UniqueID {
public:
  static UniqueID &Instance() {
    static UniqueID unique_id;
    return unique_id;
  }

  IDType Get() { return current_id_.fetch_add(1); }

private:
  UniqueID() = default;

  std::atomic<IDType> current_id_{0};
};

template <typename IDType> class LocalUniqueID {
public:
  IDType Get() { return current_id_.fetch_add(1); }

private:
  std::atomic<IDType> current_id_{0};
};
