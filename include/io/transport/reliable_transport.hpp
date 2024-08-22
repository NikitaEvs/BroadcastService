#pragma once

#include "concurrency/future.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_channel.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/executor.hpp"
#include "runtime/packet_batcher.hpp"
#include <tuple>
#include <utility>

class ReliableTransport {
public:
  using FutureT = Future<Unit>;
  using Callback = std::function<void(PeerID, Bytes)>;
  using ReliableChannelCallback = ReliableChannel::Callback;
  using ReliableChannels = std::unordered_map<PeerID, ReliableChannel>;
  using Credits = SharedBudget::CounterT;

  ReliableTransport(IExecutorPtr executor, Callback callback, HostsTable hosts,
                    const ReliableRetryPolicy &retry_policy,
                    Credits max_send_budget)
      : packet_batcher_(executor, callback), hosts_(std::move(hosts)),
        retry_policy_(retry_policy), max_send_budget_(max_send_budget) {}

  void Start(TimerService &timer_service, EventLoop &event_loop) {
    auto localhost = hosts_.ResolveLocalHost();
    auto peers = hosts_.GetPeers();
    for (auto &peer : peers) {
      auto peerhost = hosts_.Resolve(peer);
      auto [iter, ok] = channels_.emplace(
          std::piecewise_construct, std::forward_as_tuple(peer),
          std::forward_as_tuple(timer_service, hosts_.LocalID(), localhost,
                                std::move(peerhost),
                                GetReliableChannelCallback(peer), retry_policy_,
                                max_send_budget_));
      assert(ok && "Cannot add more channels");
      iter->second.Start();
    }

    for (auto &peer : peers) {
      channels_.at(peer).Register(event_loop);
    }
  }

  void Stop() {
    for (auto &[_, channel] : channels_) {
      channel.Stop();
    }
  }

  FutureT Send(PeerID peer, Bytes message) {
    if (peer == hosts_.LocalID()) {
      SendLocal(std::move(message));
      return FutureT::JustValue(Unit{});
    } else {
      return channels_.at(peer).Send(std::move(message));
    }
  }

private:
  PacketBatcher packet_batcher_;
  HostsTable hosts_;
  ReliableRetryPolicy retry_policy_;
  ReliableChannels channels_;
  Credits max_send_budget_;

  ReliableChannelCallback GetReliableChannelCallback(PeerID peer) {
    return [this, peer](PacketLinkedPtr packet) {
      if (packet->source != peer) {
        // By design, we should be able to understand source from the socket, as
        // we use connected udp sockets and the manual states that:
        //
        // "If the socket sockfd is of type SOCK_DGRAM, then addr is the address
        // to which datagrams are sent by default, and the only  address  from
        // which datagrams  are  received."
        //
        // However, there was a bug in the linux kernel:
        // https://lore.kernel.org/all/CA+FuTSfRP09aJNYRt04SS6qj22ViiOEWaWmLAwX0psk8-PGNxw@mail.gmail.com/T/#u
        // since version 4.4 This bug causes packets to be demultiplxed to the
        // wrong connected udp socket and this bug was fixed here:
        // https://github.com/torvalds/linux/commit/acdcecc61285faed359f1a3568c32089cc3a8329
        //
        // So the program running on the affected by this bug kernels will route
        // packets to the wrong connected socket and, therefore, to the wrong
        // ReliableChannel So we introduced a hack as a workaround and included
        // the source to the packets, so the user-space demultiplexing is done
        // relying on this information Although kernel-space demultiplexing will
        // be incorrect and this bug causes the packets to arrive on the wrong
        // socket, but this socket will be static and won't change over time
        // (hopefully), so the side-effect of this bug can be seen only on the
        // level of ReliableChannel but not higher.
        LOG_ERROR("RELICH",
                  "Packet source = " +
                      std::to_string(static_cast<uint32_t>(packet->source)) +
                      " doesn't equal to " +
                      std::to_string(static_cast<uint32_t>(peer)));
      }
      // packet->source = peer;
      packet_batcher_.AddPacket(packet);
    };
  }

  void SendLocal(Bytes message) {
    Packet packet;
    packet.type = PacketType::kPayloadMessage;
    packet.message = std::move(message);
    auto packet_linked = PacketLinked::Create(std::move(packet));
    packet_linked->source = hosts_.LocalID();
    packet_batcher_.AddPacket(packet_linked);
  }
};