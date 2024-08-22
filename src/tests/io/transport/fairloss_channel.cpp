#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <thread>

#include "concurrency/budget.hpp"
#include "concurrency/task_queue.hpp"
#include "concurrency/wait_group.hpp"
#include "io/timer_service.hpp"
#include "io/transport/backoff_retry.hpp"
#include "io/transport/fairloss_channel.hpp"
#include "io/transport/payload.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"
#include "util/tests/transport_util.hpp"

#include <sys/prctl.h>

using namespace std::chrono_literals;
using DurationMs = RetryPolicy::DurationMs;

// Declaration for the build flag "missing-declarations"
FairLossRetryPolicy MakeRetryPolicy(DurationMs error_timeout,
                                    double error_multiplier,
                                    DurationMs empty_write_timeout,
                                    double empty_write_multiplier,
                                    DurationMs max_timeout);
FairLossRetryPolicy MakeRetryPolicy(DurationMs error_timeout,
                                    double error_multiplier,
                                    DurationMs empty_write_timeout,
                                    double empty_write_multiplier,
                                    DurationMs max_timeout) {
  RetryPolicy error_policy(error_timeout, error_multiplier, max_timeout);
  RetryPolicy empty_write_policy(empty_write_timeout, empty_write_multiplier,
                                 max_timeout);
  return FairLossRetryPolicy(std::move(error_policy),
                             std::move(empty_write_policy));
}

// Precondition: python3 echo.py 8081
TEST(FairLossChannel, SmokeTest) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  EventLoop event_loop(100);
  event_loop.Setup();

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  auto local_addr = GetPeerAddrInfo(9000);
  auto peer_addr = GetPeerAddrInfo(8081);

  std::atomic<PacketLinkedPtr> received{nullptr};

  auto callback = [&received](PacketLinkedPtr packet) noexcept {
    received.store(packet);
  };

  auto retry_policy = MakeRetryPolicy(100, 2, 100, 2, 1000);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB
  FairLossChannel channel(timer_service, retry_policy, local_addr, peer_addr,
                          callback, budget);
  channel.Start(event_loop);

  Packet packet;
  packet.type = PacketType::kPayloadMessage;
  packet.message = Bytes{'a', 'b', 'c'};
  auto *linked = PacketLinked::Create(packet);

  channel.Send(linked);

  std::this_thread::sleep_for(200ms);

  auto *received_linked = received.load();
  ASSERT_NE(received_linked, nullptr);
  ASSERT_EQ(packet.message, received_linked->message);

  received_linked->Destroy();

  event_loop.Stop();
  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
  channel.Stop();
}

// Precondition: python3 echo.py 8082
TEST(FairLossChannel, MultiplePackets) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  EventLoop event_loop(200);
  event_loop.Setup();

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  WaitGroup wg;

  constexpr size_t num_event_loops = 4;
  wg.Add(num_event_loops);
  for (size_t i = 0; i < num_event_loops; ++i) {
    thread_pool.Execute(Lambda::Create([&event_loop, &wg]() {
      event_loop.Run();
      wg.Done();
    }));
  }

  auto local_addr = GetPeerAddrInfo(9001);
  auto peer_addr = GetPeerAddrInfo(8082);

  BatchedLFQueue<PacketLinked> received;

  auto callback = [&received](PacketLinkedPtr packet) {
    received.Push(packet);
  };

  auto retry_policy = MakeRetryPolicy(100, 2, 100, 2, 1000);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB
  FairLossChannel channel(timer_service, retry_policy, local_addr, peer_addr,
                          callback, budget);
  channel.Start(event_loop);

  constexpr size_t kNumPackets = 100;
  std::vector<Packet> packets(kNumPackets);
  for (auto &packet : packets) {
    packet.type = PacketType::kPayloadMessage;
    packet.message = Bytes{'d', 'e', 'f'};
    auto *linked = PacketLinked::Create(packet);
    channel.Send(linked);
  }

  std::this_thread::sleep_for(200ms);

  event_loop.Stop();
  wg.Wait();
  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
  channel.Stop();

  auto received_batch = received.TryPopAll();
  ASSERT_EQ(kNumPackets, received_batch.Size());

  size_t i = 0;
  while (auto *node = received_batch.PopFront()) {
    ASSERT_EQ(packets[i].message, node->message);
    node->Destroy();
    ++i;
  }
}

// Precondition: python3 echo.py 8081
// python3 echo.py 8082
// python3 echo.py ...
// python3 echo.py 8081 + num_senders - 1
TEST(FairLossChannel, MultipleChannels) {
  ThreadPool thread_pool(8);
  thread_pool.Start();

  EventLoop event_loop(200);
  event_loop.Setup();

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  constexpr size_t num_event_loops = 5;
  for (size_t i = 0; i < num_event_loops; ++i) {
    thread_pool.Execute(
        Lambda::Create([&event_loop, i]() { event_loop.Run(); }));
  }

  constexpr size_t num_senders = 3;
  std::vector<char> payloads(num_senders);
  for (size_t sender = 0; sender < num_senders; ++sender) {
    if (sender % 2 == 0) {
      payloads[sender] = 'a';
    } else if (sender % 2 == 1) {
      payloads[sender] = 'b';
    }
  }

  auto local_addr = GetPeerAddrInfo(9002);
  std::vector<sockaddr_in> peer_addrs(num_senders);
  for (size_t sender = 0; sender < num_senders; ++sender) {
    peer_addrs[sender] = GetPeerAddrInfo(8081 + static_cast<uint16_t>(sender));
  }

  std::vector<BatchedLFQueue<PacketLinked>> received(num_senders);
  std::vector<std::function<void(PacketLinkedPtr)>> callbacks;

  for (size_t sender = 0; sender < num_senders; ++sender) {
    auto &sender_received = received[sender];
    callbacks.push_back([&sender_received](PacketLinkedPtr packet) {
      sender_received.Push(packet);
    });
  }

  auto retry_policy = MakeRetryPolicy(100, 2, 100, 2, 1000);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB
  std::vector<std::unique_ptr<FairLossChannel>> channels;
  for (size_t i = 0; i < num_senders; ++i) {
    auto channel = std::make_unique<FairLossChannel>(
        timer_service, retry_policy, local_addr, peer_addrs[i],
        std::move(callbacks[i]), budget);
    channels.emplace_back(std::move(channel));
  }

  for (auto &channel : channels) {
    channel->Start(event_loop);
  }

  constexpr size_t num_packets = 50;
  constexpr size_t payload_size = 3;
  std::vector<Packet> packets(num_packets * num_senders);
  for (size_t sender = 0; sender < num_senders; ++sender) {
    thread_pool.Execute(
        Lambda::Create([&packets, &payloads, &channels, sender]() {
          Bytes payload(payload_size, payloads[sender]);
          for (size_t packet_num = sender * num_packets;
               packet_num < (sender + 1) * num_packets; ++packet_num) {
            auto &packet = packets[packet_num];
            packet.type = PacketType::kPayloadMessage;
            packet.message = payload;
            auto *linked = PacketLinked::Create(packet);
            channels[sender]->Send(linked);
          }
        }));
  }

  std::this_thread::sleep_for(200ms);

  event_loop.Stop();
  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();

  for (size_t sender = 0; sender < num_senders; ++sender) {
    auto received_batch = received[sender].TryPopAll();
    ASSERT_EQ(num_packets, received_batch.Size());

    size_t i = 0;
    while (auto *node = received_batch.PopFront()) {
      ASSERT_EQ(packets[sender * num_packets + i].message, node->message);
      node->Destroy();
      ++i;
    }
  }

  for (auto &channel : channels) {
    channel->Stop();
  }
}
