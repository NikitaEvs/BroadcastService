#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "concurrency/budget.hpp"
#include "concurrency/combine.hpp"
#include "concurrency/future.hpp"
#include "concurrency/wait_group.hpp"
#include "io/timer_service.hpp"
#include "io/transport/backoff_retry.hpp"
#include "io/transport/fairloss_channel.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_channel.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"
#include "util/instrusive_list.hpp"
#include "util/logging.hpp"
#include "util/statistics.hpp"
#include "util/tests/transport_util.hpp"

using namespace std::chrono_literals;

class ScopeGuard {
public:
  ScopeGuard(EventLoop &event_loop, ThreadPool &thread_pool,
             ReliableChannel &channel)
      : event_loop_(event_loop), thread_pool_(thread_pool), channel_(channel) {}

  ~ScopeGuard() {
    event_loop_.Stop();
    thread_pool_.Wait();
    thread_pool_.SignalStop();
    thread_pool_.WaitForStop();
    channel_.Stop();
  }

private:
  EventLoop &event_loop_;
  ThreadPool &thread_pool_;
  ReliableChannel &channel_;
};

// Precondition: python3 echo.py 8081
TEST(ReliableChannel, Retries) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  EventLoop event_loop(100);
  event_loop.Setup();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  auto local_addr = GetPeerAddrInfo(9000);
  auto peer_addr = GetPeerAddrInfo(8081);

  PeerID local_id = 0;

  std::atomic<size_t> num_retries{0};

  auto payload = Bytes{'a', 'b', 'c'};

  auto callback = [&num_retries, &payload](PacketLinkedPtr input_message) {
    LOG_DEBUG("LOSSCH", "FIre callback");
    num_retries.fetch_add(1);
    ASSERT_EQ(payload, input_message->message);
  };

  RetryPolicy error_policy(100, 2, timer_service.MaxDuration());
  RetryPolicy empty_write_policy(100, 2, timer_service.MaxDuration());
  FairLossRetryPolicy fair_loss_policy(error_policy, empty_write_policy);
  RetryPolicy msg_policy(100, 2, 800);
  ReliableRetryPolicy policy(msg_policy, fair_loss_policy);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB

  ReliableChannel channel(timer_service, local_id, local_addr, peer_addr,
                          std::move(callback), policy, budget);
  channel.Start(event_loop);

  channel.Send(payload).Detach();

  ScopeGuard guard(event_loop, thread_pool, channel);
  std::this_thread::sleep_for(2s);

  // Did not deliver duplicate messages
  ASSERT_EQ(1, num_retries.load());
}

// Precondition: python3 echo_ack.py 8082
TEST(ReliableChannel, Acks) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  EventLoop event_loop(100);
  event_loop.Setup();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  TimerService timer_service(100);
  timer_service.Register(event_loop);

  auto local_addr = GetPeerAddrInfo(9002);
  auto peer_addr = GetPeerAddrInfo(8082);

  PeerID local_id = 0;

  auto payload = Bytes{'a', 'b', 'c'};

  auto callback = [](PacketLinkedPtr /*input_message*/) noexcept {};

  RetryPolicy error_policy(100, 2, timer_service.MaxDuration());
  RetryPolicy empty_write_policy(100, 2, timer_service.MaxDuration());
  FairLossRetryPolicy fair_loss_policy(error_policy, empty_write_policy);
  RetryPolicy msg_policy(1000, 2, 2000);
  ReliableRetryPolicy policy(msg_policy, fair_loss_policy);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB

  ReliableChannel channel(timer_service, local_id, local_addr, peer_addr,
                          std::move(callback), policy, budget);
  channel.Start(event_loop);

  auto result = channel.Send(std::move(payload))
                    .Then([](Unit) { return Ok(Unit{}); })
                    .Get();
  ScopeGuard guard(event_loop, thread_pool, channel);
  ASSERT_TRUE(result.HasValue());
}

// Precondition: python3 echo_ack.py 8083
TEST(ReliableChannel, MultipleAcks) {
  constexpr size_t num_messages = 1000000;
  constexpr size_t num_workers = 8;
  constexpr size_t num_runners = 4;
  constexpr size_t max_num_events = 100;
  constexpr size_t tick_duration_ms = 100;

  ThreadPool thread_pool(num_workers);
  thread_pool.Start();

  EventLoop event_loop(max_num_events);
  event_loop.Setup();

  for (size_t i = 0; i < num_runners; ++i) {
    thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));
  }

  std::atomic<bool> stop_stats{false};
  thread_pool.Execute(Lambda::Create([&stop_stats]() {
    while (!stop_stats.load(std::memory_order_relaxed)) {
      auto snapshot = Statistics::Instance().GetSnapshot();
      std::cout << snapshot.Print();
      std::this_thread::sleep_for(10ms);
    }
  }));

  TimerService timer_service(tick_duration_ms);
  timer_service.Register(event_loop);

  auto local_addr = GetPeerAddrInfo(9003);
  auto peer_addr = GetPeerAddrInfo(8083);

  PeerID local_id = 0;

  auto callback = [](PacketLinkedPtr /*input_message*/) noexcept {};

  RetryPolicy error_policy(100, 1.5, timer_service.MaxDuration());
  RetryPolicy empty_write_policy(100, 1.5, timer_service.MaxDuration());
  FairLossRetryPolicy fair_loss_policy(error_policy, empty_write_policy);
  RetryPolicy msg_policy(1000, 2, timer_service.MaxDuration(), true);
  ReliableRetryPolicy policy(msg_policy, fair_loss_policy);
  SharedBudget::CounterT budget = 200;

  ReliableChannel channel(timer_service, local_id, local_addr, peer_addr,
                          std::move(callback), policy, budget);
  channel.Start(event_loop);

  ScopeGuard guard(event_loop, thread_pool, channel);

  auto start = std::chrono::high_resolution_clock::now();

  // std::vector<Future<Unit>> acks;
  // SharedBudget rate_limiter(100000);

  // for (size_t num_message = 1; num_message <= num_messages; ++num_message) {
  //   while (rate_limiter.Credit(1) != 1) {
  //     std::this_thread::sleep_for(10ns);
  //   }
  //   PayloadMessage message;
  //   message.id = static_cast<MessageID>(num_message);
  //   message.payload = Bytes(24, 'a');
  //   acks.push_back(channel.Send(std::move(message)).Then([&rate_limiter](Unit)
  //   {
  //     rate_limiter.TopUp(1);
  //     return Ok(Unit{});
  //   }));
  // }

  size_t num_message = 1;
  while (num_message <= num_messages) {
    std::vector<Future<Unit>> acks;
    for (size_t batch_idx = 0; batch_idx < 30000 && num_message <= num_messages;
         ++batch_idx, ++num_message) {
      auto payload = Bytes(24, 'a');
      acks.push_back(channel.Send(std::move(payload)));
    }

    auto ack = All(std::move(acks));
    auto result = ack.Get();
    ASSERT_TRUE(result.HasValue());
    ASSERT_FALSE(result.HasError());
  }

  // auto ack = All(std::move(acks));
  // auto result = ack.Get();
  stop_stats.store(true, std::memory_order_relaxed);
  // ASSERT_TRUE(result.HasValue());
  // ASSERT_FALSE(result.HasError());

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Transferred " << num_messages << " messages in "
            << duration.count() << " ms" << std::endl;
  double speed = (static_cast<double>(60 * 1000) /
                  static_cast<double>(duration.count()) * num_messages);
  std::cout << std::fixed << std::showpoint;
  std::cout << std::setprecision(2);
  std::cout << "Speed: " << speed << " messages in a minute" << std::endl;

  auto snapshot = Statistics::Instance().GetSnapshot();
  std::cout << snapshot.Print();
}

TEST(ReliableChannel, MultipleAcksListener) {
  constexpr size_t num_workers = 8;
  constexpr size_t num_runners = 4;
  constexpr size_t max_num_events = 100;
  constexpr size_t tick_duration_ms = 100;

  ThreadPool thread_pool(num_workers);
  thread_pool.Start();

  EventLoop event_loop(max_num_events);
  event_loop.Setup();

  for (size_t i = 0; i < num_runners; ++i) {
    thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));
  }

  std::atomic<bool> stop_stats{false};
  // thread_pool.Execute(Lambda::Create([&stop_stats]() {
  //   while (!stop_stats.load(std::memory_order_relaxed)) {
  //     auto snapshot = Statistics::Instance().GetSnapshot();
  //     std::cout << snapshot.Print();
  //     std::this_thread::sleep_for(50ms);
  //   }
  // }));

  TimerService timer_service(tick_duration_ms);
  timer_service.Register(event_loop);

  auto local_addr = GetPeerAddrInfo(8083);
  auto peer_addr = GetPeerAddrInfo(9003);

  PeerID local_id = 0;

  auto callback = [](PacketLinkedPtr /*input_message*/) noexcept {};

  RetryPolicy error_policy(100, 1.5, timer_service.MaxDuration());
  RetryPolicy empty_write_policy(100, 1, timer_service.MaxDuration());
  FairLossRetryPolicy fair_loss_policy(error_policy, empty_write_policy);
  RetryPolicy msg_policy(1000, 1.5, timer_service.MaxDuration());
  ReliableRetryPolicy policy(msg_policy, fair_loss_policy);
  SharedBudget::CounterT budget = 128 * 1024; // 128 KB

  ReliableChannel channel(timer_service, local_id, local_addr, peer_addr,
                          std::move(callback), policy, budget);
  channel.Start(event_loop);

  ScopeGuard guard(event_loop, thread_pool, channel);

  std::this_thread::sleep_for(1h);
}
