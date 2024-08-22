#pragma once

#include <netinet/in.h>

in_addr_t ipLookup(const char *host);

sockaddr_in GetPeerAddrInfo(uint16_t port);
