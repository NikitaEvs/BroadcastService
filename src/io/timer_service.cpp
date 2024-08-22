#include "io/timer_service.hpp"

#include <bits/types/struct_itimerspec.h>
#include <cassert>
#include <ctime>
#include <mutex>
#include <sys/timerfd.h>
#include <unistd.h>

#include "util/syscall.hpp"

TimerService::TimerService(DurationMs tick_duration)
    : tick_duration_(tick_duration) {}

void TimerService::Register(EventLoop &event_loop) {
  timer_fd_ = SetupTimerFd();
  Event tick_event{timer_fd_, this, EventType::kReadReady};
  event_loop.AddEvent(tick_event);
  LOG_INFO("TIMERS", "registered service with fd " + std::to_string(timer_fd_));
}

void TimerService::Handle(EventType type, EventLoop &event_loop) {
  Tick();
  Event tick_event{timer_fd_, this, type};
  event_loop.ModEvent(tick_event);
}

TimerService::TimerID TimerService::GetID() {
  std::lock_guard lock(mutex_);
  auto id = current_id_++;
  return id;
}

TimerService::TimerID TimerService::AddTimer(TimerPtr timer,
                                             DurationMs duration) {
  Ticks tick_duration = duration / tick_duration_;
  assert(tick_duration < kMaxTicksDuration &&
         "TimerService doesn't support too big durations");

  std::lock_guard lock(mutex_);
  auto timer_id = current_id_;
  DoAddTimer(timer, timer_id, tick_duration, lock);
  ++current_id_;

  return timer_id;
}

void TimerService::AddTimer(TimerPtr timer, TimerID id, DurationMs duration) {
  Ticks tick_duration = duration / tick_duration_;
  assert(tick_duration < kMaxTicksDuration &&
         "TimerService doesn't support too big durations");

  std::lock_guard lock(mutex_);
  DoAddTimer(timer, id, tick_duration, lock);
}

void TimerService::CancelTimer(TimerID id) {
  std::lock_guard lock(mutex_);
  cancelled_timers_.insert(id);

  LOG_DEBUG("TIMERS", "schedule to cancel timer " + std::to_string(id));
}

TimerService::DurationMs TimerService::MaxDuration() const {
  return kMaxTicksDuration * tick_duration_;
}

void TimerService::Stop() {
  std::lock_guard lock(mutex_);
  // Expire all timers
  for (Ticks i = 0; i < kMaxTicksDuration; ++i) {
    AdvanceSlot();
    auto slot_expired_timers = ExpireSlot();

    while (auto *timer = slot_expired_timers.PopFront()) {
      timer->Cancel();
    }
  }
}

void TimerService::Tick() {
  List cancelled_timers_list;
  List fired_timers_list;

  auto elapsed = GetElapsedTicks();

  {
    std::lock_guard lock(mutex_);
    auto current_tick = ticks_;
    ticks_ += elapsed;

    List expired_timers;
    for (Ticks i = 0; i < elapsed; ++i) {
      AdvanceSlot();
      auto slot_expired_timers = ExpireSlot();
      expired_timers.Insert(std::move(slot_expired_timers));
    }

    while (auto *timer = expired_timers.PopFront()) {
      auto cancelled_timer = cancelled_timers_.find(timer->id);
      if (cancelled_timer != cancelled_timers_.end()) {
        cancelled_timers_list.PushBack(timer);
        cancelled_timers_.erase(cancelled_timer);
      } else {
        fired_timers_list.PushBack(timer);
      }
    }
  }

  while (auto *timer = cancelled_timers_list.PopFront()) {
    LOG_DEBUG("TIMERS", "cancel timer with id = " + std::to_string(timer->id));
    timer->Cancel();
  }

  while (auto *timer = fired_timers_list.PopFront()) {
    LOG_DEBUG("TIMERS", "fire timer with id = " + std::to_string(timer->id));
    timer->Fire(timer->id);
  }
}

int TimerService::SetupTimerFd() {
  auto timer_fd = timerfd_create(CLOCK_REALTIME, /*flags=*/0);
  CANNOT_FAIL(timer_fd, "Cannot create a new timer fd");

  auto tick_duration_s = tick_duration_ / 1000;
  auto tick_duration_ns = (tick_duration_ - tick_duration_s * 1000) * 1'000'000;

  itimerspec spec;
  spec.it_value.tv_sec = static_cast<time_t>(tick_duration_s);
  spec.it_value.tv_nsec = static_cast<__time_t>(tick_duration_ns);
  spec.it_interval.tv_sec = static_cast<time_t>(tick_duration_s);
  spec.it_interval.tv_nsec = static_cast<__time_t>(tick_duration_ns);

  auto status =
      timerfd_settime(timer_fd, /*flags=*/0, &spec, /*old_spec=*/nullptr);
  CANNOT_FAIL(status, "Cannot set up a new timer");

  return timer_fd;
}

TimerService::Ticks TimerService::GetElapsedTicks() {
  assert(timer_fd_ != 0 && "TimerService is in invalid state");

  Ticks elapsed_ticks;
  auto status = read(timer_fd_, &elapsed_ticks, sizeof(Ticks));
  if (status != sizeof(Ticks)) {
    CANNOT_FAIL(-1, "TimerService read invalid value");
  }

  return elapsed_ticks;
}

void TimerService::AdvanceSlot() {
  ++slot_;
  if (slot_ == kMaxTicksDuration) {
    slot_ = 0;
  }
}

TimerService::List TimerService::ExpireSlot() {
  auto &slot_list = timers_[slot_];

  List expired_timers;
  List remaining_timers;

  while (auto *timer = slot_list.PopFront()) {
    if (ticks_ >= timer->deadline) {
      expired_timers.PushFront(timer);
    } else {
      remaining_timers.PushFront(timer);
    }
  }

  slot_list = std::move(remaining_timers);
  return expired_timers;
}

void TimerService::DoAddTimer(TimerPtr timer, TimerID id, Ticks tick_duration,
                              std::lock_guard<Mutex> &) {
  timer->deadline = ticks_ + tick_duration;
  timer->id = id;
  auto slot = timer->deadline % kMaxTicksDuration;
  timers_[slot].PushBack(timer);

  LOG_DEBUG("TIMERS", "registered new timer " + std::to_string(timer->id) +
                          " with duration " + std::to_string(tick_duration) +
                          " ticks");
}
