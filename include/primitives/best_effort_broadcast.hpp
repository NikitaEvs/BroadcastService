#pragma once

#include "io/transport/hosts.hpp"
#include "io/transport/reliable_transport.hpp"
#include "runtime/runtime.hpp"

class BestEffortBroadcast {
public:
  using ReliableTransportCallback = ReliableTransport::Callback;
  using Callback = ReliableTransportCallback;
  using FutureT = ReliableTransport::FutureT;
  using FuturesT = std::vector<FutureT>;

  BestEffortBroadcast(Runtime &runtime, Callback callback)
      : runtime_(runtime), callback_(std::move(callback)) {}

  FuturesT Broadcast(Bytes message) {
    auto peers = runtime_.GetHosts().GetPeers();
    auto localID = runtime_.GetHosts().LocalID();

    FuturesT futures;
    futures.reserve(peers.size() + 1);

    LOG_DEBUG("BRB", "broadcast message from " +
                         std::to_string(static_cast<uint32_t>(localID)) +
                         " to " + std::to_string(peers.size() + 1) + " peers");

    for (const auto &peer : peers) {
      futures.push_back(runtime_.GetTransport().Send(peer, message));
    }
    // Deliver locally
    futures.push_back(
        runtime_.GetTransport().Send(localID, std::move(message)));

    return futures;
  }

  ReliableTransportCallback GetReliableTransportCallback() {
    return [this](PeerID peer, Bytes message) noexcept {
      LOG_DEBUG("BRB", "perfect links delivery from " +
                           std::to_string(static_cast<uint32_t>(peer)));
      callback_(peer, std::move(message));
    };
  }

private:
  Runtime &runtime_;
  Callback callback_;
};
