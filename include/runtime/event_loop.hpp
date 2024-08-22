#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/epoll.h>

#include "concurrency/tsan_sync.hpp"
#include "util/noncopyable.hpp"

enum class EventType {
  kReadReady,
  kWriteReady,
  kError,
};

EventType ToEventType(uint32_t epoll_events);
uint32_t ToEpollEvents(EventType type);
std::string ToString(EventType type);

class EventLoop;

class IHandler {
public:
  virtual ~IHandler() = default;
  virtual void Handle(EventType type, EventLoop &event_loop) = 0;

  void AfterEpollWait() { TSAN_ACQUIRE }

  void BeforeEpollCtl() { TSAN_RELEASE }

private:
  TSAN_SYNC
};
using IHandlerPtr = IHandler *;

struct Event {
  int fd{0};
  IHandlerPtr handler{nullptr};
  EventType type{EventType::kError};

  Event() = default;
  Event(int fd, IHandlerPtr handler, EventType type);

  epoll_event AsEpollEvent() const;
  std::string String() const;
};

class EventLoop : private NonCopyable {
public:
  explicit EventLoop(int max_events_number);

  void Setup();

  void Run();

  void AddEvent(const Event &event);
  void ModEvent(const Event &event);
  void DelEvent(int fd);

  void Stop();

  static epoll_event AsEpollEvent(const Event &event);

private:
  class StopHandler : public IHandler {
  public:
    StopHandler(bool &stop_flag) : stop_flag_(stop_flag) {}
    void Handle(EventType, EventLoop &) { stop_flag_ = true; }

  private:
    bool &stop_flag_;
  };

  int epoll_descriptor_;
  int stop_event_descriptor_;
  int max_events_number_;

  void EpollCreate();
  void SetupStopEvent();

  void TriggerStopEvent();

  void EpollWaitLoop();

  bool HandleEpollEvent(const epoll_event &epoll_event);
};
