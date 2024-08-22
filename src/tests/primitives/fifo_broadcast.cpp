#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_channel.hpp"
#include "io/transport/reliable_transport.hpp"
#include "parser.hpp"
#include "primitives/best_effort_broadcast.hpp"
#include "primitives/fifo_broadcast.hpp"
#include "primitives/uniform_reliable_broadcast.hpp"
#include "runtime/runtime.hpp"
#include "util/logging.hpp"

using namespace std::chrono_literals;

class FifoBroadcastTest : public testing::Test {
protected:
  using Host = Parser::Host;

  void SetUp() override {
    runtime_ = std::make_unique<Runtime>(GetRuntimeConfig());
    runtime_->Start();
  }

public:
  RuntimeConfig GetRuntimeConfig() {
    RuntimeConfig config;
    config.threadpool_workers = 8;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 50;
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
    host_list.emplace_back(3, localhost, 9003);
    hosts_ = HostsTable(std::move(host_list), localID);
  }

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
TEST_F(FifoBroadcastTest, SmokeTest1) {
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

  FIFOBroadcast fifo(*runtime_, callback, hosts_);

  runtime_->SetTransport(fifo.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto future = fifo.Broadcast(Bytes(1, 'a'));
  future.Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}

TEST_F(FifoBroadcastTest, SmokeTest2) {
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

  FIFOBroadcast fifo(*runtime_, callback, hosts_);

  runtime_->SetTransport(fifo.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto future = fifo.Broadcast(Bytes(1, 'b'));
  future.Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}

TEST_F(FifoBroadcastTest, SmokeTest3) {
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

  FIFOBroadcast fifo(*runtime_, callback, hosts_);

  runtime_->SetTransport(fifo.GetReliableTransportCallback(), hosts_,
                         GetReliableRetryPolicy());
  auto &transport = runtime_->GetTransport();
  transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

  auto future = fifo.Broadcast(Bytes(1, 'c'));
  future.Get();

  std::this_thread::sleep_for(1s);

  std::lock_guard lock(mutex);

  ScopeGuard guard(*runtime_);
  ASSERT_EQ(3, received);
  for (size_t i = 0; i < 3; ++i) {
    auto symbol = static_cast<char>(static_cast<int>('a') + i);
    ASSERT_EQ(Bytes(1, symbol), delivered[i]);
  }
}

struct FifoMessage {
  uint32_t sequence;

  Bytes Marshal() {
    Bytes bytes(Size());
    auto data = bytes.data();
    data = BinaryMarshalPrimitiveType(sequence, data);
    return bytes;
  }

  void Unmarshal(const Bytes &bytes) {
    sequence = BinaryUnmarshalPrimitiveType<uint32_t>(bytes.data());
  }

  size_t Size() const { return sizeof(uint32_t); }
};

void FifoTest(PeerID localID, FifoBroadcastTest &test);
void FifoTest(PeerID localID, FifoBroadcastTest &test) {
  test.SetupHosts(localID);

  std::atomic<bool> stop_stats{false};
  test.runtime_->GetThreadPool().Execute(Lambda::Create([&stop_stats]() {
    while (!stop_stats.load(std::memory_order_relaxed)) {
      auto snapshot = Statistics::Instance().GetSnapshot();
      std::cout << snapshot.Print();
      std::this_thread::sleep_for(10ms);
    }
  }));

  constexpr PeerID num_hosts = 4;
  constexpr uint32_t num_messages = 200000;
  constexpr size_t batch_size = 3000;
  constexpr size_t link_packet_limit = 18000;

  SpinLock mutex;
  std::unordered_map<PeerID, std::vector<uint32_t>> sequences;
  for (uint8_t host_id = 0; host_id < num_hosts; ++host_id) {
    sequences[host_id] = std::vector<uint32_t>(0);
  }
  size_t received = 0;

  auto callback = [&sequences, &mutex, &received](PeerID peer,
                                                  Bytes message) noexcept {
    FifoMessage fifo_message;
    fifo_message.Unmarshal(message);

    std::lock_guard lock(mutex);
    ++received;
    sequences[peer].push_back(fifo_message.sequence);
  };

  FIFOBroadcast fifo(*test.runtime_, callback, test.hosts_);

  test.runtime_->SetTransport(fifo.GetReliableTransportCallback(), test.hosts_,
                              test.GetReliableRetryPolicy());
  auto &transport = test.runtime_->GetTransport();
  transport.Start(test.runtime_->GetTimerService(),
                  test.runtime_->GetEventLoop());

  ScopeGuard guard(*test.runtime_);

  auto start = std::chrono::high_resolution_clock::now();

  UniformReliableBroadcastRateLimiter limiter(num_hosts, link_packet_limit);

  uint32_t message_sequence = 0;
  while (message_sequence < num_messages) {
    std::vector<Future<Unit>> futures;

    while (limiter.Fire() && message_sequence < num_messages) {
      FifoMessage fifo_message;
      fifo_message.sequence = message_sequence;
      ++message_sequence;
      auto buffer = fifo_message.Marshal();
      auto future = fifo.Broadcast(std::move(buffer));
      futures.push_back(std::move(future));
    }
    limiter.Reset();

    auto result = All(std::move(futures)).Get();
    ASSERT_TRUE(result.HasValue());
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  while (true) {
    std::this_thread::sleep_for(1s);
    std::lock_guard lock(mutex);
    std::cout << "Received " << received << " messages out of "
              << num_hosts * num_messages;
    if (received == num_hosts * num_messages) {
      std::cout << ", all messages were received, stop waiting" << std::endl;
      break;
    } else {
      std::cout << ", keep waiting" << std::endl;
    }
  }

  stop_stats.store(true, std::memory_order_relaxed);

  ASSERT_EQ(num_hosts * num_messages, received);
  for (PeerID host = 0; host < num_hosts; ++host) {
    auto &sequence = sequences[host];
    ASSERT_EQ(num_messages, sequence.size());
    ASSERT_TRUE(std::is_sorted(sequence.begin(), sequence.end()));
  }

  std::cout << "Broadcasted " << num_messages << " messages in "
            << duration.count() << " ms" << std::endl;
  double speed = (static_cast<double>(60 * 1000) /
                  static_cast<double>(duration.count()) * num_messages);
  std::cout << std::fixed << std::showpoint;
  std::cout << std::setprecision(2);
  std::cout << "Speed: " << speed << " messages in a minute" << std::endl;
}

// Condition: run FifoTest1, FifoTest2, FifoTest3, FifoTest4 simultaniously
TEST_F(FifoBroadcastTest, FifoTest1) { FifoTest(0, *this); }
TEST_F(FifoBroadcastTest, FifoTest2) { FifoTest(1, *this); }
TEST_F(FifoBroadcastTest, FifoTest3) { FifoTest(2, *this); }
TEST_F(FifoBroadcastTest, FifoTest4) { FifoTest(3, *this); }