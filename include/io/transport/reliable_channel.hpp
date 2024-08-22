#pragma once

#include "concurrency/budget.hpp"
#include "concurrency/contract.hpp"
#include "concurrency/future.hpp"
#include "concurrency/mrmw_map.hpp"
#include "concurrency/spinlock.hpp"
#include "io/timer_service.hpp"
#include "io/transport/backoff_retry.hpp"
#include "io/transport/fairloss_channel.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/udp_socket.hpp"
#include "runtime/event_loop.hpp"
#include "util/logging.hpp"
#include "util/result.hpp"
#include "util/statistics.hpp"
#include "util/unique_id.hpp"

#include <functional>

struct ReliableRetryPolicy {
  ReliableRetryPolicy(RetryPolicy msg_policy,
                      FairLossRetryPolicy fair_loss_policy)
      : msg_policy(std::move(msg_policy)),
        fair_loss_policy(std::move(fair_loss_policy)) {}

  RetryPolicy msg_policy;
  FairLossRetryPolicy fair_loss_policy;
};

class ReliableChannel {
public:
  using Callback = std::function<void(PacketLinkedPtr)>;
  using FairLossCallback = FairLossChannel::Callback;
  using RetryCallback = std::function<void()>;
  using AddrInfo = FairLossChannel::AddrInfo;
  using TimerID = TimerService::TimerID;
  using MessageIDs = std::unordered_set<MessageID>;
  using Credits = SharedBudget::CounterT;

  using FutureT = Future<Unit>;
  using PromiseT = Promise<Unit>;

  ReliableChannel(TimerService &timer_service, PeerID local_id, AddrInfo info,
                  AddrInfo peer_info, Callback callback,
                  const ReliableRetryPolicy &retry_policy,
                  Credits max_send_budget)
      : timer_service_(timer_service), local_id_(local_id),
        fair_loss_(timer_service, retry_policy.fair_loss_policy,
                   std::move(info), std::move(peer_info), GetFairLossCallback(),
                   std::move(max_send_budget)),
        callback_(std::move(callback)), retry_policy_(retry_policy.msg_policy) {
  }

  void Start(EventLoop &event_loop) {
    Start();
    Register(event_loop);
  }

  void Start() { fair_loss_.Start(); }

  void Register(EventLoop &event_loop) {
    fair_loss_.Register(event_loop);
    LOG_INFO("RELICH", "start fair-loss");
  }

  void Stop() {
    fair_loss_.Stop();

    auto promises = message_entries_.Drain();
    for (auto &entry : promises) {
      timer_service_.CancelTimer(entry.timer_id);
      std::move(entry.promise).SetError(make_error_code(Error::internal));
    }

    LOG_INFO("RELICH", "stop fair-loss and drain promises");
  }

  FutureT Send(Bytes message) {
    auto [f, p] = MakeContract<Unit>();

    // Get an id for the retry timer
    auto timer_id = timer_service_.GetID();

    // Register the message entry
    // MessageEntry entry(timer_id, std::move(p),
    //                    message.Size() + Packet::kPacketOverhead);
    auto message_id =
        UniqueID<UniqueClass::kMessageID, MessageID>::Instance().Get();
    MessageEntry entry(timer_id, std::move(p), 1);
    message_entries_.Set(message_id, std::move(entry));

    // Create a packet to send
    auto packet = CreatePacket(std::move(message), PacketType::kPayloadMessage,
                               message_id);

    // Set up a retry timer
    auto retry_timer = BackoffRetryTimer::Create(
        timer_service_, GetBackoffRetryCallback(packet), retry_policy_);
    timer_service_.AddTimer(retry_timer, timer_id, retry_policy_.GetTimeout());

    LOG_DEBUG("RELICH",
              "register send message with id = " + std::to_string(message_id) +
                  ", timer_id = " + std::to_string(timer_id));

    // Pack and send a message using FairLossChannel
    DoSend(std::move(packet));

    return std::move(f);
  }

private:
  struct MessageEntry {
    TimerID timer_id;
    PromiseT promise;
    SharedBudget::CounterT credits;

    MessageEntry(TimerID timer_id, PromiseT promise,
                 const SharedBudget::CounterT &credits)
        : timer_id(timer_id), promise(std::move(promise)), credits(credits) {}
  };
  using MessageEntries = MRMWUnorderedMap<MessageID, MessageEntry, SpinLock>;

  TimerService &timer_service_;
  PeerID local_id_;
  FairLossChannel fair_loss_;
  Callback callback_;
  RetryPolicy retry_policy_;
  MessageEntries message_entries_;

  SpinLock mutex_;
  MessageIDs delivered_;

  FairLossCallback GetFairLossCallback() {
    return [this](PacketLinkedPtr packet) {
      // TODO: Add PacketBatcher
      HandlePacket(packet);
    };
  }

  RetryCallback GetBackoffRetryCallback(const Packet &packet) {
    return [this, packet]() {
      // Add credits for the retry because messages could be lost
      // fair_loss_.AddSendCredits(message.Size() + Packet::kPacketOverhead);
      fair_loss_.AddSendCredits(1);
      DoSend(packet);
    };
  }

  Packet CreatePacket(Bytes payload, PacketType type, MessageID id) {
    Packet packet;
    packet.type = type;
    packet.id = id;
    packet.source = local_id_;
    packet.message = std::move(payload);
    return packet;
  }

  void DoSend(Packet packet) {
    auto packet_linked = PacketLinked::Create(std::move(packet));
    fair_loss_.Send(packet_linked);
  }

  void HandlePacket(PacketLinkedPtr packet) {
    if (packet->type == PacketType::kAckMessage) {
      AckMessage message;
      message.Unmarshal(std::move(packet->message));
      HandleAckMessage(std::move(message));
      packet->Destroy();
    } else if (packet->type == PacketType::kPayloadMessage) {
      HandlePayloadMessage(packet);
    } else {
      CANNOT_FAIL(-1, "FariLossCallback: unknown message type");
    }
  }

  void HandleAckMessage(AckMessage message) {
    LOG_DEBUG("RELICH", "receive from fair loss ack message with " +
                            std::to_string(message.acks.size()) + " acks");
    for (auto message_id : message.acks) {
      Statistics::Instance().Change("acks_received", 1);
      auto entry = message_entries_.Take(message_id);
      if (entry.has_value()) {
        LOG_DEBUG("RELICH",
                  "cancel timer with id = " + std::to_string(entry->timer_id) +
                      ", fulfil promise and add credits to the budget");
        timer_service_.CancelTimer(entry->timer_id);
        fair_loss_.AddSendCredits(entry->credits);

        std::move(entry->promise).SetValue(Unit{});
      }
    }
  }

  void HandlePayloadMessage(PacketLinkedPtr packet) {
    bool not_delivered = false;

    {
      std::lock_guard lock(mutex_);
      auto [_, not_exists] = delivered_.emplace(packet->id);
      not_delivered = not_exists;
    }

    // Send ack back
    AckMessage ack_message;
    ack_message.acks = {packet->id};
    Bytes ack_payload(ack_message.Size());
    ack_message.MarshalExtract(ack_payload);

    // Try fast path: send directly to the socket without sendign queue
    Packet ack_packet = CreatePacket(
        std::move(ack_payload), PacketType::kAckMessage,
        UniqueID<UniqueClass::kMessageID, MessageID>::Instance().Get());
    Bytes ack_packet_payload(ack_packet.Size());
    ack_packet.Marshal(ack_packet_payload);
    auto result = fair_loss_.socket_.Write(ack_packet_payload);
    if (result == WriteStatus::kConnRefused ||
        result == WriteStatus::kWouldBlock) {
      // Fast path failed, send to the queue
      auto linked_pkt = PacketLinked::Create(std::move(ack_packet));
      fair_loss_.Send(linked_pkt);
    }

    LOG_DEBUG("RELICH", "receive from fair loss payload message with id = " +
                            std::to_string(packet->id));
    if (not_delivered) {
      LOG_DEBUG(
          "RELICH",
          "message was not received, call callback with content: " +
              std::string(packet->message.begin(), packet->message.end()));

      // Fire callback
      callback_(packet);
    } else {
      packet->Destroy();
      LOG_DEBUG("RELICH", "message was received before, do not call callback");
    }
  }
};
