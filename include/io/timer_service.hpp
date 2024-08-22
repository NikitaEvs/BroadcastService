#pragma once

#include <array>
#include <mutex>
#include <unordered_set>

#include "concurrency/spinlock.hpp"
#include "io/timer.hpp"
#include "runtime/event_loop.hpp"
#include "util/instrusive_list.hpp"

// Timers wheel: schema 4 from
// http://www.cs.columbia.edu/~nahum/w6998/papers/sosp87-timing-wheels.pdf
class TimerService : public IHandler {
public:
  using DurationMs = uint64_t; // milliseconds
  using Ticks = ITimer::Ticks;
  using TimerID = ITimer::TimerID;
  using Timer = ITimer;
  using TimerPtr = ITimerPtr;
  using List = IntrusiveList<Timer>;
  using Mutex = SpinLock;

  static constexpr size_t kMaxTicksDuration = 1000;

  explicit TimerService(DurationMs tick_duration);

  void Register(EventLoop &event_loop);

  void Handle(EventType type, EventLoop &event_loop) override;

  TimerID GetID();

  TimerID AddTimer(TimerPtr timer, DurationMs duration);

  void AddTimer(TimerPtr timer, TimerID id, DurationMs duration);

  void CancelTimer(TimerID id);

  DurationMs MaxDuration() const;

  void Stop();

private:
  const DurationMs tick_duration_;
  int timer_fd_{0};

  Mutex mutex_;

  // Guarded by mutex_
  Ticks ticks_{0};
  std::array<List, kMaxTicksDuration> timers_;
  size_t slot_{0};
  TimerID current_id_{0};
  std::unordered_set<TimerID> cancelled_timers_;

  void Tick();

  int SetupTimerFd();

  Ticks GetElapsedTicks();

  void AdvanceSlot();

  List ExpireSlot();

  void DoAddTimer(TimerPtr timer, TimerID id, Ticks tick_duration,
                  std::lock_guard<Mutex> &);
};
