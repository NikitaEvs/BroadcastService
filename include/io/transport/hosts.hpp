#pragma once

#include <cstdint>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>

#include "parser.hpp"

using PeerID = uint8_t;

class HostsTable {
public:
  using Host = Parser::Host;
  // Faster than unordered for small sizes
  using Hosts = std::map<PeerID, Host>;
  using HostsList = std::vector<Host>;
  using AddrInfo = sockaddr_in;
  using Peers = std::vector<PeerID>;

  HostsTable() = default;

  HostsTable(HostsList hosts_list, PeerID localID)
      : hosts_list_(std::move(hosts_list)), localID_(localID) {
    peers_.reserve(hosts_list_.size());
    for (const auto &host : hosts_list_) {
      hosts_[static_cast<uint8_t>(host.id)] = host;
      if (host.id != localID) {
        peers_.push_back(static_cast<uint8_t>(host.id));
      }
    }
  }

  Host GetHost(PeerID id) { return hosts_[id]; }

  PeerID LocalID() const { return localID_; }

  Host LocalHost() { return GetHost(localID_); }

  Peers GetPeers() const { return peers_; }

  size_t GetNetworkSize(bool with_me) const {
    if (with_me) {
      return peers_.size() + 1;
    } else {
      return peers_.size();
    }
    // return with_me ? peers_.size() + 1 : peers_.size();
  }

  AddrInfo Resolve(PeerID id) {
    auto host = GetHost(id);
    AddrInfo info;
    info.sin_family = kSocketFamily;
    info.sin_port = host.port;
    info.sin_addr.s_addr = host.ip;
    return info;
  }

  AddrInfo ResolveLocalHost() { return Resolve(localID_); }

private:
  static constexpr sa_family_t kSocketFamily = AF_INET;

  HostsList hosts_list_;
  Hosts hosts_;
  PeerID localID_;
  Peers peers_;
};
