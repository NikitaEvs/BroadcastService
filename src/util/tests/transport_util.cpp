#include "util/tests/transport_util.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <stdexcept>

in_addr_t ipLookup(const char *host) {
  struct addrinfo hints, *res;
  char addrstr[128];
  void *ptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags |= AI_CANONNAME;

  if (getaddrinfo(host, NULL, &hints, &res) != 0) {
    throw std::runtime_error("Could not resolve host `" + std::string(host) +
                             "` to IP: " + std::string(strerror(errno)));
  }

  while (res) {
    inet_ntop(res->ai_family, res->ai_addr->sa_data, addrstr, 128);

    switch (res->ai_family) {
    case AF_INET:
      ptr = &(reinterpret_cast<struct sockaddr_in *>(res->ai_addr))->sin_addr;
      inet_ntop(res->ai_family, ptr, addrstr, 128);
      freeaddrinfo(res);
      return inet_addr(addrstr);
      break;
    // case AF_INET6:
    //     ptr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
    //     break;
    default:
      break;
    }
    res = res->ai_next;
  }

  freeaddrinfo(res);

  throw std::runtime_error("No host resolves to IPv4");
}

sockaddr_in GetPeerAddrInfo(uint16_t port) {
  sockaddr_in addr_info;
  addr_info.sin_family = AF_INET;
  addr_info.sin_port = htons(port);
  addr_info.sin_addr.s_addr = ipLookup("0.0.0.0");

  return addr_info;
}
