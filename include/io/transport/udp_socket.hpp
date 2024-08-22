#pragma once

#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <asm-generic/socket.h>
#include <linux/filter.h>
#include <sys/socket.h>

#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "util/logging.hpp"
#include "util/syscall.hpp"

enum class WriteStatus {
  kWouldBlock,
  kConnRefused,
  kOk,
};

class UDPSocket {
public:
  using AddrInfo = HostsTable::AddrInfo;
  using FdType = int;

  explicit UDPSocket(AddrInfo info) : info_(std::move(info)) {}

  void Setup() {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    CANNOT_FAIL(socket_fd_, "UDPSocket failed to create a new socket");

    const int enable = 1;
    auto set_result =
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    CANNOT_FAIL(set_result,
                "UDPSocket failed to set SO_REUSEADDR on the socket");
    set_result =
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
    CANNOT_FAIL(set_result,
                "UDPSocket failed to set SO_REUSEPORT on the socket");

    LockSocket(socket_fd_);

    auto *sockaddr = reinterpret_cast<struct sockaddr *>(&info_);
    auto bind_result = bind(socket_fd_, sockaddr, sizeof(info_));
    CANNOT_FAIL(bind_result, "UDPSocket failed to bind on the address");

    read_event_fd_ = socket_fd_;
    write_event_fd_ = dup(read_event_fd_);
    CANNOT_FAIL(write_event_fd_, "UDPSocket failed to duplicate a fd");

    LOG_INFO("SOCKET", "set up a new socket with fd " +
                           std::to_string(socket_fd_) + " on " +
                           ToString(info_) + " port");
  }

  void Close() {
    if (socket_fd_ != 0) {
      CANNOT_FAIL(shutdown(socket_fd_, SHUT_RDWR),
                  "UDPSocket failed to shutdown the socket");
      CANNOT_FAIL(close(socket_fd_), "UDPSocket failed to close the socket");
      LOG_DEBUG("SOCKET", "close socket with fd " + std::to_string(socket_fd_));
    }
  }

  void ConnectTo(AddrInfo info) {
    // Use established UDP socket
    auto *sockaddr = reinterpret_cast<struct sockaddr *>(&info);
    auto connect_result = connect(socket_fd_, sockaddr, sizeof(info));
    CANNOT_FAIL(connect_result, "UDPSocket failed to connect");
    LOG_DEBUG("SOCKET", "connect socket with fd " + std::to_string(socket_fd_) +
                            " to port " + ToString(info));
    UnlockSocket(socket_fd_);
  }

  WriteStatus Write(const Bytes &bytes) {
    auto result = write(socket_fd_, bytes.data(), bytes.size());
    if (result == -1) {
      if (errno == EAGAIN) {
        LOG_DEBUG("SOCKET",
                  "write to fd " + std::to_string(socket_fd_) + " would block");
        return WriteStatus::kWouldBlock;
      } else if (errno == ECONNREFUSED) {
        LOG_DEBUG("SOCKET",
                  "write to fd " + std::to_string(socket_fd_) + " refused");
        return WriteStatus::kConnRefused;
      } else {
        CANNOT_FAIL(result, "UDPSocket fail to write to fd " +
                                std::to_string(socket_fd_));
        return WriteStatus::kConnRefused;
      }
    }
    LOG_DEBUG("SOCKET", "wrote " + std::to_string(result) +
                            " bytes to socket's buffer with fd " +
                            std::to_string(socket_fd_));
    return WriteStatus::kOk;
  }

  ssize_t Read(Bytes &bytes) {
    auto result = read(socket_fd_, bytes.data(), bytes.size());
    if (result == -1) {
      if (errno == EAGAIN || errno == ECONNREFUSED) {
        LOG_DEBUG("SOCKET", "read from fd " + std::to_string(socket_fd_) +
                                " would block");
        return result;
      } else {
        CANNOT_FAIL(result, "UDPSocket fail to read from fd " +
                                std::to_string(socket_fd_));
        return result;
      }
    }
    LOG_DEBUG("SOCKET", "read " + std::to_string(result) + " bytes from fd " +
                            std::to_string(socket_fd_));
    return result;
  }

  FdType GetFd() const { return socket_fd_; }

  FdType GetReadEventFd() const { return read_event_fd_; }

  FdType GetWriteEventFd() const { return write_event_fd_; }

private:
  AddrInfo info_;
  FdType socket_fd_{0};
  FdType read_event_fd_{0};
  FdType write_event_fd_{0};

  static void LockSocket(int socket) {
    // BPF to filter all packets
    // https://www.tcpdump.org/papers/bpf-usenix93.pdf -- byte code definition
    // We need to do it to mitigate race condition between socket.bind and
    // socket.connect See:
    // https://lkml.kernel.org/netdev/CANP3RGfAT199GyqWC7Wbr2983jO1vaJ1YJBSSXtFJmGJaY+wiQ@mail.gmail.com/
    struct sock_filter code[] = {
        {0x06, 0, 0, 0x00000000}, // ret #0
    };
    struct sock_fprog prog = {
        /*.len=*/1,
        /*.filter=*/code,
    };

    auto set_result =
        setsockopt(socket, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog));
    CANNOT_FAIL(set_result, "UDPSocket failed to set lock BPF on the socket");
  }

  static void UnlockSocket(int socket) {
    int dummy = 0;
    auto set_result =
        setsockopt(socket, SOL_SOCKET, SO_DETACH_FILTER, &dummy, sizeof(dummy));
    CANNOT_FAIL(set_result, "UDPSocket failed to reset lock BPF on the socket");
  }
};
