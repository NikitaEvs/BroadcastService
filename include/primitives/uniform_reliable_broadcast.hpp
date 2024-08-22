#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "concurrency/mrmw_set.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_transport.hpp"
#include "primitives/best_effort_broadcast.hpp"
#include "runtime/runtime.hpp"
#include "util/logging.hpp"
#include "util/statistics.hpp"
#include "util/unique_id.hpp"

class URBMessage : public ISerializable {
public:
  PeerID source;
  MessageID urb_id;
  Bytes message;

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(source, data);

    data = BinaryMarshalPrimitiveType(urb_id, data);

    std::copy(message.begin(), message.end(), data);
  }

  void MarshalExtract(Bytes &bytes) override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(source, data);

    data = BinaryMarshalPrimitiveType(urb_id, data);

    std::move(message.begin(), message.end(), data);
  }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());
    const auto *data = bytes.data();

    source = BinaryUnmarshalPrimitiveType<PeerID>(data);
    data += sizeof(PeerID);

    urb_id = BinaryUnmarshalPrimitiveType<MessageID>(data);
    data += sizeof(MessageID);

    const auto message_size = bytes.size() - HeaderSize();
    message.resize(message_size);
    std::copy(bytes.begin() + static_cast<Bytes::difference_type>(HeaderSize()),
              bytes.end(), message.begin());
  }

  size_t Size() const override { return HeaderSize() + message.size(); }

  size_t HeaderSize() const override {
    return sizeof(PeerID) + sizeof(MessageID);
  }
};

struct URBIdentifier {
  PeerID peer;
  MessageID urb_id;

  bool operator==(const URBIdentifier &other) const {
    return peer == other.peer && urb_id == other.urb_id;
  }
};

template <> struct std::hash<URBIdentifier> {
  size_t operator()(const URBIdentifier &identifier) const noexcept {
    return ((std::hash<PeerID>()(identifier.peer) ^
             (std::hash<MessageID>()(identifier.urb_id) << 1)) >>
            1);
  }
};

class UniformReliableBroadcast {
public:
  using ReliableTransportCallback = ReliableTransport::Callback;
  using BestEffortBroadcastCallback = BestEffortBroadcast::Callback;
  using Callback = std::function<void(URBIdentifier, Bytes)>;
  using FutureT = ReliableTransport::FutureT;
  using FuturesT = std::vector<FutureT>;
  using Mutex = SpinLock;

  UniformReliableBroadcast(Runtime &runtime, Callback callback,
                           const HostsTable &hosts)
      : runtime_(runtime), callback_(std::move(callback)),
        beb_(runtime, GetBestEffortBroadcastCallback()),
        localID_(hosts.LocalID()),
        network_size_(hosts.GetNetworkSize(/*with_me=*/true)) {}

  BestEffortBroadcastCallback GetBestEffortBroadcastCallback() {
    return [this](PeerID /*peer*/, Bytes message) {
      URBMessage urb_message;
      urb_message.Unmarshal(message);

      URBIdentifier identifier;
      identifier.peer = urb_message.source;
      identifier.urb_id = urb_message.urb_id;

      // Do not process message if it was already delivered
      if (WasDelivered(identifier)) {
        return;
      }
      DeliveredGarbageCollection(identifier.peer);

      size_t num_acks = ++acks_[identifier];

      LOG_DEBUG("URB",
                "beb delivery message with id = " +
                    std::to_string(urb_message.urb_id) + ", source = " +
                    std::to_string(static_cast<uint32_t>(urb_message.source)) +
                    ", num_acks = " + std::to_string(num_acks));

      if (!pending_.Has(identifier)) {
        LOG_DEBUG(
            "URB",
            "message with id = " + std::to_string(urb_message.urb_id) +
                ", source = " +
                std::to_string(static_cast<uint32_t>(urb_message.source)) +
                " doesn't exist in pendings, retransmit");

        pending_.Set(identifier, urb_message.message);
        Bytes buffer(urb_message.Size());
        urb_message.MarshalExtract(buffer);
        auto futures = beb_.Broadcast(std::move(buffer));
        for (auto &future : futures) {
          future.Detach();
        }
      } else if (CanDeliver(num_acks) && !WasDelivered(identifier)) {
        LOG_DEBUG(
            "URB",
            "message with id = " + std::to_string(urb_message.urb_id) +
                ", source = " +
                std::to_string(static_cast<uint32_t>(urb_message.source)) +
                " will be delivered");

        delivered_.insert(identifier);
        auto message = pending_.Take(identifier);
        acks_.erase(identifier);

        if (!message.has_value()) {
          LOG_ERROR("URB", "URB callback cannot find pending message");
          return;
        }
        callback_(identifier, std::move(*message));
      } else {
        LOG_DEBUG(
            "URB",
            "message with id = " + std::to_string(urb_message.urb_id) +
                ", source = " +
                std::to_string(static_cast<uint32_t>(urb_message.source)) +
                " exists in pendings, but cannot be delivered");
      }
    };
  }

  ReliableTransportCallback GetReliableTransportCallback() {
    return beb_.GetReliableTransportCallback();
  }

  MessageID GetNextID() { return ids_.Get(); }

  FuturesT Broadcast(Bytes message, MessageID urb_id) {
    LOG_DEBUG("URB", "broadcast message with id = " + std::to_string(urb_id) +
                         " from " +
                         std::to_string(static_cast<uint32_t>(localID_)));

    URBMessage urb_message;
    urb_message.source = localID_;
    urb_message.urb_id = urb_id;
    urb_message.message = std::move(message);

    Bytes buffer(urb_message.Size());
    urb_message.Marshal(buffer);

    URBIdentifier identifier;
    identifier.peer = localID_;
    identifier.urb_id = urb_id;

    pending_.Set(std::move(identifier), std::move(urb_message.message));

    return beb_.Broadcast(std::move(buffer));
  }

  FuturesT Broadcast(Bytes message) {
    return Broadcast(std::move(message), GetNextID());
  }

  void CleanDeliveredPrefix(PeerID peer, MessageID prefix) {
    std::lock_guard lock(delivered_prefix_to_clean_mutex_);
    delivered_prefix_to_clean_[peer] = prefix;
  }

private:
  Runtime &runtime_;
  Callback callback_;
  BestEffortBroadcast beb_;
  PeerID localID_;
  size_t network_size_;

  LocalUniqueID<MessageID> ids_;

  MRMWUnorderedMap<URBIdentifier, Bytes, SpinLock> pending_;

  // Guarded by packet_batcher
  std::unordered_set<URBIdentifier> delivered_;
  std::unordered_map<URBIdentifier, size_t> acks_;
  std::unordered_map<PeerID, MessageID> delivered_prefix_cleaned_;

  SpinLock delivered_prefix_to_clean_mutex_;
  std::unordered_map<PeerID, MessageID>
      delivered_prefix_to_clean_; // guarded by delivered_prefix_to_clean_mutex_

  bool CanDeliver(size_t num_acks) { return num_acks > network_size_ / 2; }

  bool WasDelivered(URBIdentifier identifier) {
    auto peer_delivered_prefix =
        delivered_prefix_cleaned_.find(identifier.peer);
    if (peer_delivered_prefix != delivered_prefix_cleaned_.end() &&
        peer_delivered_prefix->second >= identifier.urb_id) {
      return true;
    }
    return delivered_.count(identifier) != 0;
  }

  void DeliveredGarbageCollection(PeerID peer) {
    MessageID segment_to_clean_end;
    {
      std::lock_guard lock(delivered_prefix_to_clean_mutex_);
      auto prefix_to_clean_it = delivered_prefix_to_clean_.find(peer);
      if (prefix_to_clean_it == delivered_prefix_to_clean_.end()) {
        return;
      }
      segment_to_clean_end = prefix_to_clean_it->second;
    }

    auto cleaned_prefix_it = delivered_prefix_cleaned_.find(peer);
    MessageID segment_to_clean_start =
        cleaned_prefix_it == delivered_prefix_cleaned_.end()
            ? 0
            : cleaned_prefix_it->second + 1;

    for (MessageID urb_id = segment_to_clean_start;
         urb_id <= segment_to_clean_end; ++urb_id) {
      URBIdentifier identifier{peer, urb_id};
      auto cleaned = delivered_.erase(identifier);
      if (cleaned == 1) {
        Statistics::Instance().Change("deleted_urb_msgs", 1);
      }
    }

    if (segment_to_clean_start <= segment_to_clean_end) {
      delivered_prefix_cleaned_[peer] = segment_to_clean_end;
    }
  }
};

class UniformReliableBroadcastRateLimiter {
public:
  UniformReliableBroadcastRateLimiter(size_t network_size,
                                      size_t link_packet_limit) {
    // Number of packets generated by one broadcast
    size_t packet_multiplier = network_size * (network_size - 1);

    batch_size_ =
        std::max(static_cast<size_t>(1), link_packet_limit / packet_multiplier);
  }

  bool Fire() {
    if (current_fire_ == batch_size_) {
      return false;
    }
    ++current_fire_;
    return true;
  }

  void Reset() { current_fire_ = 0; }

private:
  size_t batch_size_;
  size_t current_fire_{0};
};
