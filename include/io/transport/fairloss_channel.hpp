#pragma once

#include "concurrency/budget.hpp"
#include "concurrency/mrmw_set.hpp"
#include "concurrency/spinlock.hpp"
#include "concurrency/task_queue.hpp"
#include "io/retryable_handler.hpp"
#include "io/timer.hpp"
#include "io/timer_service.hpp"
#include "io/transport/backoff_retry.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/udp_socket.hpp"
#include "runtime/event_loop.hpp"
#include "util/logging.hpp"
#include "util/statistics.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <sys/types.h>
#include <thread>

struct FairLossRetryPolicy {
  FairLossRetryPolicy(RetryPolicy error_policy, RetryPolicy empty_write_policy)
      : error_policy(std::move(error_policy)),
        empty_write_policy(std::move(empty_write_policy)) {}

  RetryPolicy error_policy;
  RetryPolicy empty_write_policy;
};

class FairLossChannel {
public:
  using Callback = std::function<void(PacketLinkedPtr)>;
  using AddrInfo = HostsTable::AddrInfo;
  using Credits = SharedBudget::CounterT;

  // TODO: Refactor FairLossChannel arguments
  FairLossChannel(TimerService &timer_service, FairLossRetryPolicy retry_policy,
                  AddrInfo info, AddrInfo peer_info, Callback callback,
                  Credits max_budget)
      : timer_service_(timer_service), retry_policy_(std::move(retry_policy)),
        socket_(std::move(info)), peer_info_(peer_info),
        callback_(std::move(callback)), write_handler_(*this),
        read_handler_(*this), send_budget_(max_budget, max_budget) {}

  void Start(EventLoop &event_loop) {
    Start();
    Register(event_loop);
  }

  void Start() {
    socket_.Setup();
    socket_.ConnectTo(peer_info_);
  }

  void Register(EventLoop &event_loop) {
    Event write_event{socket_.GetWriteEventFd(), &write_handler_,
                      EventType::kWriteReady};
    write_handler_.SetEvent(write_event);
    event_loop.AddEvent(write_event);

    Event read_event{socket_.GetReadEventFd(), &read_handler_,
                     EventType::kReadReady};
    read_handler_.SetEvent(read_event);
    event_loop.AddEvent(read_event);

    LOG_INFO("LOSSCH", "start on the link to :" + ToString(peer_info_));
  }

  void Stop() {
    socket_.Close();

    auto remaining_packets = outgoing_packets_.TryPopAll();
    while (auto *node = remaining_packets.PopFront()) {
      node->Destroy();
    }

    LOG_INFO("LOSSCH", "stop on the link to :" + ToString(peer_info_));
  }

  void Send(PacketLinkedPtr packet) {
    if (outgoing_message_ids_.EstimateSize() >= kOutgoingMessageCap) {
      LOG_DEBUG("LOSSCH", "packet will probably overflow the write queue, drop "
                          "the packet with id = " +
                              std::to_string(packet->id));
      Statistics::Instance().Change("dropped_pkts", 1);
      packet->Destroy();
      return;
    }

    if (outgoing_message_ids_.Insert(packet->id)) {
      LOG_DEBUG("LOSSCH", "register send to " + ToString(peer_info_) +
                              " with id = " + std::to_string(packet->id));
      Statistics::Instance().Change("write_queue_size", 1);
      outgoing_packets_.Push(packet);
    } else {
      LOG_DEBUG("LOSSCH", "packet already exists in the queue with id = " +
                              std::to_string(packet->id));
      Statistics::Instance().Change("duplicate_pkts", 1);
      packet->Destroy();
    }
  }

  void AddSendCredits(SharedBudget::CounterT credits) {
    send_budget_.TopUp(credits);
  }

private:
  class WriteEventHandler : public IRetryableHandler {
  public:
    using RetryCallback = TimerFunction::Callback;
    using DurationMs = RetryPolicy::DurationMs;

    explicit WriteEventHandler(FairLossChannel &chan)
        : IRetryableHandler(chan.timer_service_), chan_(chan),
          error_policy_(chan_.retry_policy_.error_policy),
          empty_write_policy_(chan_.retry_policy_.empty_write_policy) {}

    void Handle(EventType type, EventLoop &event_loop) final {
      if (type == EventType::kError) {
        LOG_DEBUG("LOSSCH", "handle write error, try to reschedule");
        Reschedule(event_loop, error_policy_);
        return;
      }
      assert(type == event_.type);

      auto budget = chan_.send_budget_.Credit();
      auto old_budget = budget;
      if (budget == 0) {
        LOG_DEBUG("LOSSCH", "don't have budget to send, try to reschedule");
        Reschedule(event_loop, empty_write_policy_);
        return;
      } else {
        LOG_DEBUG("LOSSCH",
                  "budget to send = " + std::to_string(budget) + ", continue");
      }

      auto batch = chan_.outgoing_packets_.TryPopAll();
      if (batch.Empty()) {
        LOG_DEBUG("LOSSCH", "batch to write is empty, try to reschedule");
        chan_.send_budget_.TopUp(budget);
        Reschedule(event_loop, empty_write_policy_);
        return;
      } else {
        empty_write_policy_.Reset();
      }
      LOG_DEBUG("LOSSCH", "batch to write has " + std::to_string(batch.Size()) +
                              " packets");

      size_t num_sent_pkts = 0;

      WriteStatus write_status = WriteStatus::kConnRefused;
      while (auto *next = batch.PopFront()) {
        size_t message_size = 1;

        // Only PayloadMessage spend budget, but AckMessages won't be send if
        // the budget is zero
        if (next->type == PacketType::kPayloadMessage) {
          if (message_size > budget) {
            batch.PushFront(next);
            LOG_DEBUG("LOSSCH", "write exceeded the budget, return "
                                "packet to buffer");
            break;
          } else {
            LOG_DEBUG("LOSSCH", "write with cost " +
                                    std::to_string(message_size) +
                                    " is in the budget, send the packet");
            budget -= message_size;
          }
        }

        Bytes send_buffer(next->Size());
        next->Marshal(send_buffer);
        write_status = chan_.socket_.Write(send_buffer);
        if (write_status == WriteStatus::kWouldBlock ||
            write_status == WriteStatus::kConnRefused) {
          if (next->type == PacketType::kPayloadMessage) {
            budget += message_size;
          }

          batch.PushFront(next);
          LOG_DEBUG("LOSSCH", "write would block or refused to connect, return "
                              "packet to buffer");

          break;
        } else {
          LOG_DEBUG("LOSSCH", "wrote packet to socket");
          Statistics::Instance().Change("write_queue_size", -1);
          Statistics::Instance().Change("pkts_sent", 1);

          chan_.outgoing_message_ids_.Delete(next->id);
          next->Destroy();
        }
        ++num_sent_pkts;

        // Pause a little to let kernel handle the packet
        BusyWait(kBusyWaitCycles);
      }

      LOG_DEBUG("LOSSCH", "sent " + std::to_string(num_sent_pkts) +
                              " pkts for budget " + std::to_string(old_budget) +
                              " remaining budget is " + std::to_string(budget));

      if (!batch.Empty()) {
        LOG_DEBUG("LOSSCH", "batch has " + std::to_string(batch.Size()) +
                                " remaining packets, return them");
        chan_.outgoing_packets_.Push(std::move(batch));
      }

      if (budget > 0) {
        chan_.send_budget_.TopUp(budget);
      }

      if (write_status == WriteStatus::kConnRefused) {
        LOG_DEBUG("LOSSCH", "write refused to connect, try to reschedule");
        Reschedule(event_loop, error_policy_);
        return;
      } else {
        error_policy_.Reset();
      }

      Rearm(event_loop);
    }

    void Reschedule(EventLoop &event_loop, RetryPolicy &policy) {
      auto socket = std::to_string(event_.fd);
      auto timeout = policy.GetTimeout();
      if (Retry(event_loop, policy)) {
        LOG_DEBUG("LOSSCH", "rescheduled write event on " + socket + " after " +
                                std::to_string(timeout) + "ms");
      } else {
        LOG_DEBUG("LOSSCH", "did not reschedule write event, max "
                            "retry is reached");
      }
    }

  private:
    static constexpr size_t kBusyWaitCycles = 9;

    FairLossChannel &chan_;
    RetryPolicy error_policy_;
    RetryPolicy empty_write_policy_;
  };

  class ReadEventHandler : public IRetryableHandler {
  public:
    using RetryCallback = BackoffRetryTimer::Callback;
    using DurationMs = RetryPolicy::DurationMs;

    explicit ReadEventHandler(FairLossChannel &chan)
        : IRetryableHandler(chan.timer_service_), chan_(chan),
          error_policy_(chan.retry_policy_.error_policy) {}

    void Handle(EventType type, EventLoop &event_loop) final {
      if (type == EventType::kError) {
        LOG_DEBUG("LOSSCH", "handle read error, try to reschedule");
        Reschedule(event_loop, error_policy_);
        return;
      } else {
        error_policy_.Reset();
      }
      assert(type == EventType::kReadReady);

      Bytes bytes(kReadBufferSize);
      while (true) {
        constexpr ssize_t kWouldBlock = -1;
        auto size = chan_.socket_.Read(bytes);
        if (size == kWouldBlock) {
          LOG_DEBUG("LOSSCH", "read would block, stop reading");
          break;
        } else if (size == 0) {
          LOG_DEBUG("LOSSCH", "read 0 bytes, continue reading");
          continue;
        }
        LOG_DEBUG("LOSSCH", "read " + std::to_string(size) +
                                " bytes, call callback on the packet");

        Statistics::Instance().Change("pkts_received", 1);
        Packet packet;
        packet.Unmarshal(
            Bytes(bytes.begin(), bytes.begin() + static_cast<long>(size)));
        auto *packet_linked = PacketLinked::Create(std::move(packet));
        chan_.callback_(packet_linked);
      }

      Rearm(event_loop);
    }

    void Reschedule(EventLoop &event_loop, RetryPolicy &policy) {
      auto socket = std::to_string(event_.fd);
      auto timeout = policy.GetTimeout();
      if (Retry(event_loop, policy)) {
        LOG_DEBUG("LOSSCH", "rescheduled read event on " + socket + " after " +
                                std::to_string(timeout) + "ms");
      } else {
        LOG_DEBUG("LOSSCH", "did not reschedule read event on " + socket +
                                ", max retry is reached");
      }
    }

  private:
    static constexpr size_t kReadBufferSize = 65536;
    FairLossChannel &chan_;
    RetryPolicy error_policy_;
  };

  TimerService &timer_service_;
  FairLossRetryPolicy retry_policy_;

public:
  UDPSocket socket_;

private:
  AddrInfo peer_info_;
  Callback callback_;

  // FairLoss channel owns outgoing packets
  BatchedLFQueue<PacketLinked> outgoing_packets_;
  MRMWUnorderedSet<MessageID, SpinLock> outgoing_message_ids_;
  static constexpr size_t kOutgoingMessageCap = 20000;

  WriteEventHandler write_handler_;
  ReadEventHandler read_handler_;

  SharedBudget send_budget_;
};
