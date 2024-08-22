#include <gtest/gtest.h>
#include <netinet/in.h>
#include <thread>

#include "io/transport/udp_socket.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/thread_pool.hpp"
#include "util/tests/transport_util.hpp"

using namespace std::chrono_literals;

// Precondition: run python3 echo.py 8081
TEST(UDPSocket, SmokeTest) {
  auto local_addr = GetPeerAddrInfo(9000);
  auto peer_addr = GetPeerAddrInfo(8081);

  UDPSocket socket(local_addr);
  socket.Setup();
  socket.ConnectTo(peer_addr);

  Bytes message = {'a', 'b', 'c'};
  auto write_result = socket.Write(message);
  ASSERT_EQ(WriteStatus::kOk, write_result);

  std::this_thread::sleep_for(100ms);

  Bytes buffer(1024);
  auto read_result = socket.Read(buffer);
  ASSERT_EQ(3, read_result);

  buffer.resize(static_cast<size_t>(read_result));
  ASSERT_EQ(message, buffer);

  socket.Close();
}

// Precondition: run python3 echo.py 8081
// ...
// python3 echo.py 8081 + num_sockets - 1
TEST(UDPSocket, MultipleSockets) {
  constexpr size_t num_sockets = 3;

  auto local_addr = GetPeerAddrInfo(9001);
  std::vector<UDPSocket> sockets;
  sockets.reserve(num_sockets);

  for (uint16_t port = 8081; port < 8081 + num_sockets; ++port) {
    sockets.emplace_back(local_addr);
    auto peer_addr = GetPeerAddrInfo(port);
    sockets.back().Setup();
    sockets.back().ConnectTo(peer_addr);
  }

  Bytes message = {'a', 'b', 'c'};

  for (auto &socket : sockets) {
    auto write_result = socket.Write(message);
    ASSERT_EQ(WriteStatus::kOk, write_result);
  }

  std::this_thread::sleep_for(200ms);

  for (auto &socket : sockets) {
    Bytes buffer(1024);
    auto read_result = socket.Read(buffer);
    ASSERT_EQ(message.size(), read_result);

    buffer.resize(static_cast<size_t>(read_result));
    ASSERT_EQ(message, buffer);

    socket.Close();
  }
}
