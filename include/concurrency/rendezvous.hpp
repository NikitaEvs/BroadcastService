#pragma once

#include <atomic>

class Rendezvous {
public:
  bool Producer() { return state_.exchange(true); }

  bool Consumer() { return state_.exchange(true); }

private:
  std::atomic<bool> state_{false};
};
