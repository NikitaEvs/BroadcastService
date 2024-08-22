#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <thread>

#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/reliable_channel.hpp"
#include "parser.hpp"
#include "primitives/best_effort_broadcast.hpp"
#include "runtime/runtime.hpp"

using namespace std::chrono_literals;

class BestEffortBroadcastTest : public testing::Test {
protected:
  using Host = Parser::Host;

  void SetUp() override {
    runtime_ = std::make_unique<Runtime>(GetRuntimeConfig());
    runtime_->Start();
  }

  RuntimeConfig GetRuntimeConfig() {
    RuntimeConfig config;
    config.threadpool_workers = 8;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 200;
    return config;
  }

  ReliableRetryPolicy GetReliableRetryPolicy() {
    auto timer_service_max_timeout = runtime_->GetTimerService().MaxDuration();
    RetryPolicy error_policy(100, 1.5, timer_service_max_timeout);
    RetryPolicy empty_write_policy(100, 1.5, timer_service_max_timeout);
    FairLossRetryPolicy fair_loss_policy(std::move(error_policy),
                                         std::move(empty_write_policy));
    RetryPolicy msg_policy(1000, 3, timer_service_max_timeout);
    ReliableRetryPolicy policy(std::move(msg_policy),
                               std::move(fair_loss_policy));
    return policy;
  }

  void SetupHosts(PeerID localID) {
    std::vector<Host> host_list;

    std::string localhost = "localhost";
    host_list.emplace_back(0, localhost, 9000);
    host_list.emplace_back(1, localhost, 9001);
    host_list.emplace_back(2, localhost, 9002);
    hosts_ = HostsTable(std::move(host_list), localID);
  }

protected:
  std::unique_ptr<Runtime> runtime_;
  HostsTable hosts_;
};

class ScopeGuard {
public:
  explicit ScopeGuard(Runtime &runtime) : runtime_(runtime) {}

  ~ScopeGuard() { runtime_.Stop(); }

private:
  Runtime &runtime_;
};

// Condition: run SmokeTest1, SmokeTest2, SmokeTest3 simultaniously
TEST_F(BestEffortBroadcastTest, SmokeTest1) {
  SetupHosts(0);

  SpinLock mutex;
  std::vector<Bytes> delivered(3);
  size_t received = 0;
  auto callback = [&delivered, &mutex, &received](PeerID peer,
                                                  Bytes message) noexcept {
    std::lock_guard lock(mutex);
    ++received;
    delivered[peer] = std::move(message);
  };

  BestEffortBroadcast beb(*runtime_, callback);
  runtime_->SetTransport(beb.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto futures = beb.Broadcast(Bytes(1, 'a'));
  All(std::move(futures)).Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}

TEST_F(BestEffortBroadcastTest, SmokeTest2) {
  SetupHosts(1);

  SpinLock mutex;
  std::vector<Bytes> delivered(3);
  size_t received = 0;
  auto callback = [&delivered, &mutex, &received](PeerID peer,
                                                  Bytes message) noexcept {
    std::lock_guard lock(mutex);
    ++received;
    delivered[peer] = std::move(message);
  };

  BestEffortBroadcast beb(*runtime_, callback);
  runtime_->SetTransport(beb.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto futures = beb.Broadcast(Bytes(1, 'b'));
  All(std::move(futures)).Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}

TEST_F(BestEffortBroadcastTest, SmokeTest3) {
  SetupHosts(2);

  SpinLock mutex;
  std::vector<Bytes> delivered(3);
  size_t received = 0;
  auto callback = [&delivered, &mutex, &received](PeerID peer,
                                                  Bytes message) noexcept {
    std::lock_guard lock(mutex);
    ++received;
    delivered[peer] = std::move(message);
  };

  BestEffortBroadcast beb(*runtime_, callback);
  runtime_->SetTransport(beb.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto futures = beb.Broadcast(Bytes(1, 'c'));
  All(std::move(futures)).Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}