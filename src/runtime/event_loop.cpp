#include "runtime/event_loop.hpp"

#include <asm-generic/errno-base.h>
#include <cassert>
#include <sstream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

#include "util/logging.hpp"
#include "util/syscall.hpp"

EventType ToEventType(uint32_t epoll_events) {
  if (epoll_events & EPOLLIN) {
    return EventType::kReadReady;
  } else if (epoll_events & EPOLLOUT) {
    return EventType::kWriteReady;
  } else if (epoll_events & EPOLLERR) {
    return EventType::kError;
  } else {
    assert(false && "ToEventType unknown epoll_events");
  }
  return {};
}

uint32_t ToEpollEvents(EventType type) {
  uint32_t events = EPOLLONESHOT;
  switch (type) {
  case EventType::kReadReady:
    events |= EPOLLIN;
    break;
  case EventType::kWriteReady:
    events |= EPOLLOUT;
    break;
  case EventType::kError:
    events |= EPOLLERR;
    break;
  default:
    assert(false && "ToEpollEvents event with an unknown type");
  }

  return events;
}

std::string ToString(EventType type) {
  switch (type) {
  case EventType::kReadReady:
    return "read ready";
  case EventType::kWriteReady:
    return "write ready";
  case EventType::kError:
    return "error";
  default:
    return "unknown type";
  }
}

Event::Event(int fd, IHandlerPtr handler, EventType type)
    : fd(fd), handler(handler), type(type) {}

epoll_event Event::AsEpollEvent() const {
  auto events = ToEpollEvents(type);
  epoll_data_t epoll_data;
  epoll_data.ptr = handler;
  epoll_event epoll_event{events, epoll_data};
  return epoll_event;
}

std::string Event::String() const {
  std::stringstream ss;
  ss << ToString(type) << " event on fd " << fd;
  return ss.str();
}

EventLoop::EventLoop(int max_events_number)
    : max_events_number_(max_events_number) {}

void EventLoop::Setup() {
  EpollCreate();
  SetupStopEvent();
}

void EventLoop::Run() { EpollWaitLoop(); }

void EventLoop::AddEvent(const Event &event) {
  auto epoll_event = event.AsEpollEvent();

  event.handler->BeforeEpollCtl();
  CANNOT_FAIL(
      epoll_ctl(epoll_descriptor_, EPOLL_CTL_ADD, event.fd, &epoll_event),
      "epoll_ctl_add failed");
  LOG_DEBUG("EVENTS", "added " + event.String());
}

void EventLoop::ModEvent(const Event &event) {
  auto epoll_event = event.AsEpollEvent();

  event.handler->BeforeEpollCtl();
  CANNOT_FAIL(
      epoll_ctl(epoll_descriptor_, EPOLL_CTL_MOD, event.fd, &epoll_event),
      "epoll_ctl_mod failed");
  LOG_DEBUG("EVENTS", "modified " + event.String());
}

void EventLoop::DelEvent(int fd) {
  CANNOT_FAIL(epoll_ctl(epoll_descriptor_, EPOLL_CTL_DEL, fd, nullptr),
              "epoll_ctl_del failed");
  LOG_DEBUG("EVENTS", "deleted event with descriptor " + std::to_string(fd));
}

void EventLoop::Stop() { TriggerStopEvent(); }

void EventLoop::EpollCreate() {
  constexpr int flags = 0;
  auto epoll_descriptor = epoll_create1(flags);
  CANNOT_FAIL(epoll_descriptor, "epoll creation failed");
  epoll_descriptor_ = epoll_descriptor;
  LOG_INFO("EVENTS", "epoll is created with descriptor " +
                         std::to_string(epoll_descriptor));
}

void EventLoop::SetupStopEvent() {
  auto stop_event_descriptor = eventfd(/*count=*/0, /*flags=*/0);
  CANNOT_FAIL(stop_event_descriptor, "eventfd creation failed");
  stop_event_descriptor_ = stop_event_descriptor;

  epoll_data_t epoll_data;
  epoll_data.fd = stop_event_descriptor;
  epoll_event epoll_event{EPOLLIN, epoll_data};
  CANNOT_FAIL(epoll_ctl(epoll_descriptor_, EPOLL_CTL_ADD, stop_event_descriptor,
                        &epoll_event),
              "epoll_ctl_add for the stop event failed");
}

void EventLoop::TriggerStopEvent() {
  int64_t stop_value = 1;
  CANNOT_FAIL(write(stop_event_descriptor_,
                    reinterpret_cast<void *>(&stop_value), sizeof(stop_value)),
              "write to the stop event failed");
  LOG_INFO("EVENTS", "trigger stop for the epoll with descriptor " +
                         std::to_string(epoll_descriptor_));
}

void EventLoop::EpollWaitLoop() {
  LOG_DEBUG("EVENTS", "start wait loop");

  std::vector<epoll_event> input_events(
      static_cast<size_t>(max_events_number_));

  bool stop_flag = false;
  while (!stop_flag) {
    constexpr int kNoTimeout = -1;
    auto events_num = epoll_wait(epoll_descriptor_, input_events.data(),
                                 max_events_number_, kNoTimeout);
    LOG_DEBUG("EVENTS",
              "finish waiting with " + std::to_string(events_num) + " events");
    if (events_num == -1) {
      if (errno == EINTR) {
        LOG_DEBUG("EVENTS", "epoll received EINTR, continue waiting");
        continue;
      } else {
        CANNOT_FAIL(events_num, "epoll wait failed");
      }
    }

    for (int event_idx = 0; event_idx < events_num; ++event_idx) {
      if (!HandleEpollEvent(input_events[static_cast<size_t>(event_idx)])) {
        LOG_DEBUG("EVENTS", "epoll gets the stop signal, stop wait loop");
        stop_flag = true;
      }
    }
  }
}

bool EventLoop::HandleEpollEvent(const epoll_event &epoll_event) {
  if (epoll_event.data.fd == stop_event_descriptor_) {
    return false;
  }
  EventType type = ToEventType(epoll_event.events);
  LOG_DEBUG("EVENTS", "handle " + ToString(type));
  auto *handler_ptr = reinterpret_cast<IHandlerPtr>(epoll_event.data.ptr);
  handler_ptr->AfterEpollWait();
  handler_ptr->Handle(type, *this);
  return true;
}
