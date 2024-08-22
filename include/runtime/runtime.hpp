#pragma once

#include "io/timer_service.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/reliable_channel.hpp"
#include "io/transport/reliable_transport.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/thread_pool.hpp"
#include <cstddef>
#include <utility>

struct RuntimeConfig {
  size_t threadpool_workers;
  size_t eventloop_runners;
  int eventloop_max_num_events;
  size_t timerservice_tick_duration;
  size_t max_send_budget;
};

class Runtime {
public:
  explicit Runtime(RuntimeConfig config)
      : config_(std::move(config)), thread_pool_(config_.threadpool_workers),
        event_loop_(config_.eventloop_max_num_events),
        timer_service_(config_.timerservice_tick_duration) {}

  void Start() {
    thread_pool_.Start();
    event_loop_.Setup();
    timer_service_.Register(event_loop_);

    for (size_t runner = 0; runner < config_.eventloop_runners; ++runner) {
      thread_pool_.Execute(Lambda::Create([this]() { event_loop_.Run(); }));
    }
  }

  void Stop() {
    event_loop_.Stop();
    thread_pool_.Wait();
    thread_pool_.SignalStop();
    thread_pool_.WaitForStop();
    timer_service_.Stop();
  }

  // Possible memory leak, call only to stop everything without care
  void Shutdown() {
    event_loop_.Stop();
    thread_pool_.Shutdown();
  }

  ThreadPool &GetThreadPool() { return thread_pool_; }

  EventLoop &GetEventLoop() { return event_loop_; }

  TimerService &GetTimerService() { return timer_service_; }

  void SetTransport(ReliableTransport::Callback callback, HostsTable hosts,
                    const ReliableRetryPolicy &policy) {
    hosts_ = hosts;
    transport_ = std::make_unique<ReliableTransport>(
        &thread_pool_, std::move(callback), std::move(hosts), policy,
        config_.max_send_budget);
  }

  ReliableTransport &GetTransport() { return *transport_; }

  HostsTable &GetHosts() { return hosts_; }

private:
  RuntimeConfig config_;

  ThreadPool thread_pool_;
  EventLoop event_loop_;
  TimerService timer_service_;
  std::unique_ptr<ReliableTransport> transport_;
  HostsTable hosts_;
};

class RuntimeScopeGuard {
public:
  explicit RuntimeScopeGuard(Runtime &runtime) : runtime_(runtime) {}

  ~RuntimeScopeGuard() { runtime_.Stop(); }

private:
  Runtime &runtime_;
};
