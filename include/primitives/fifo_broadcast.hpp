#pragma once

#include "concurrency/contract.hpp"
#include "concurrency/future.hpp"
#include "concurrency/mrmw_map.hpp"
#include "concurrency/promise.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_transport.hpp"
#include "primitives/uniform_reliable_broadcast.hpp"
#include "util/statistics.hpp"
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>

class FIFOBroadcast {
public:
  using ReliableTransportCallback = ReliableTransport::Callback;
  using UniformReliableBroadcastCallback = UniformReliableBroadcast::Callback;
  using Callback = std::function<void(PeerID, Bytes)>;
  using FutureT = ReliableTransport::FutureT;
  using FuturesT = std::vector<FutureT>;
  using Mutex = SpinLock;
  using Pending = std::map<MessageID, Bytes>;
  using Pendings = std::unordered_map<PeerID, Pending>;
  using NextID = std::unordered_map<PeerID, MessageID>;

  FIFOBroadcast(Runtime &runtime, Callback callback, const HostsTable &hosts)
      : runtime_(runtime), callback_(std::move(callback)),
        urb_(runtime, GetUniformReliableBroadcastCallback(), hosts),
        localID_(hosts.LocalID()) {}

  UniformReliableBroadcastCallback GetUniformReliableBroadcastCallback() {
    return [this](URBIdentifier identifier, Bytes message) {
      const auto packet_src = identifier.peer;

      if (next_ids_[packet_src] == identifier.urb_id) {
        ++next_ids_[packet_src];
        LOG_DEBUG("FIFO",
                  "receive urb broadcast from " +
                      std::to_string(static_cast<uint32_t>(packet_src)) +
                      " with urb_id == next_id, urb_id = " +
                      std::to_string(identifier.urb_id) + ", trigger delivery");
        Deliver(packet_src, identifier.urb_id, std::move(message));
      } else {
        LOG_DEBUG("FIFO",
                  "receive urb broadcast from " +
                      std::to_string(static_cast<uint32_t>(packet_src)) +
                      " with urb_id != next_id, urb_id = " +
                      std::to_string(identifier.urb_id) +
                      ", next_id = " + std::to_string(next_ids_[packet_src]) +
                      ", add to pending");
        pendings_[packet_src].emplace(
            std::piecewise_construct, std::forward_as_tuple(identifier.urb_id),
            std::forward_as_tuple(std::move(message)));
      }

      auto current_message_it = pendings_[packet_src].begin();
      while (next_ids_[packet_src] == current_message_it->first) {
        ++next_ids_[packet_src];
        auto ready_to_deliver_message_it = current_message_it;
        auto ready_to_deliver_id = ready_to_deliver_message_it->first;
        auto ready_to_deliver_message =
            std::move(ready_to_deliver_message_it->second);
        ++current_message_it;

        pendings_[packet_src].erase(ready_to_deliver_message_it);

        Deliver(packet_src, ready_to_deliver_id,
                std::move(ready_to_deliver_message));
      }
    };
  }

  ReliableTransportCallback GetReliableTransportCallback() {
    return urb_.GetReliableTransportCallback();
  }

  FutureT Broadcast(Bytes message) {
    auto [f, p] = MakeContract<Unit>();
    Statistics::Instance().Change("pending_futures", 1);

    auto message_id = urb_.GetNextID();
    broadcast_message_entries_.Set(message_id, std::move(p));

    LOG_DEBUG("FIFO",
              "urb broadcast message with id = " + std::to_string(message_id));

    auto futures = urb_.Broadcast(std::move(message), message_id);
    for (auto &future : futures) {
      future.Detach();
    }

    return std::move(f);
  }

private:
  using PromiseT = Promise<Unit>;
  using BroadcastMessageEntry = PromiseT;
  using BroadcastMessageEntres =
      MRMWUnorderedMap<MessageID, BroadcastMessageEntry, SpinLock>;

  Runtime &runtime_;
  Callback callback_;
  UniformReliableBroadcast urb_;
  PeerID localID_;

  Pendings pendings_;
  NextID next_ids_;

  BroadcastMessageEntres broadcast_message_entries_;

  void Deliver(PeerID peer, MessageID id, Bytes message) {
    LOG_DEBUG("FIFO", "trigger fifo delivery for the message with id = " +
                          std::to_string(id) + " from peer " +
                          std::to_string(static_cast<uint32_t>(peer)));
    urb_.CleanDeliveredPrefix(peer, id);
    if (peer == localID_) {
      auto entry = broadcast_message_entries_.Take(id);
      if (entry.has_value()) {
        Statistics::Instance().Change("pending_futures", -1);
        std::move(*entry).SetValue(Unit{});
      }
    }
    callback_(peer, std::move(message));
  }
};
