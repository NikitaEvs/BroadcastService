#include "concurrency/futex.hpp"

#include <asm-generic/errno-base.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

#include "util/syscall.hpp"

static void Futex(uint32_t *uaddr, int futex_op, uint32_t val) {
  auto errcode = syscall(SYS_futex, uaddr, futex_op, val, nullptr, nullptr, 0);
  if (errcode == -1 && errno == EAGAIN) {
    return;
  }
  CANNOT_FAIL(errcode, "futex syscall failed");
}

void AtomicWait(std::atomic<uint32_t> &atomic, uint32_t value) {
  auto uaddr = AtomicAddr(atomic);
  Futex(uaddr, FUTEX_WAIT, value);
}

Key AtomicAddr(std::atomic<uint32_t> &atomic) {
  static_assert(std::is_standard_layout_v<std::atomic<uint32_t>>);
  return reinterpret_cast<uint32_t *>(&atomic);
}

void AtomicWakeOne(Key key) { Futex(key, FUTEX_WAKE, 1); }

void AtomicWakeAll(Key key) {
  // According to the futex(2) / Futex operations / FUTEX_WAKE
  uint32_t all_waiters = static_cast<uint32_t>(INT_MAX);
  Futex(key, FUTEX_WAKE, all_waiters);
}
