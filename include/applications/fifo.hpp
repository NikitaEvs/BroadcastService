#pragma once

#include <cstdint>
#include <memory>

#include "applications/application.hpp"
#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "parser.hpp"
#include "primitives/fifo_broadcast.hpp"
#include "primitives/uniform_reliable_broadcast.hpp"
#include "runtime/runtime.hpp"

struct FIFOMessage {
  // There is a sequence number on PayloadMessage level, do not use the fact
  // that the message payload is sequence number
  static constexpr size_t kNumMessages = 8;
  // Zero value means invalid message
  uint32_t messages[8]{};

  Bytes Marshal() const {
    const auto size = sizeof(uint32_t) * 8;
    Bytes bytes(size);
    BinaryMarshalArrayType(messages, 8, bytes.data());
    return bytes;
  }

  void Unmarshal(const Bytes &bytes) {
    assert(bytes.size() >= sizeof(uint32_t) * 8);
    BinaryUnmarshalArrayType(messages, 8, bytes.data());
  }
};

class FIFO : public IApplication {
public:
  using SignalHandler = IApplication::SignalHandler;

  explicit FIFO(Parser &parser)
      : local_id_(static_cast<PeerID>(parser.id())),
        hosts_table_(parser.hosts(), local_id_),
        output_path_(parser.outputPath()), config_path_(parser.configPath()) {
    ParseConfig();
    runtime_ = std::make_unique<Runtime>(GetRuntimeConfig());
  }

  void Start() final {
    output_.open(output_path_, std::ios::in | std::ios::out | std::ios::trunc);
    runtime_->Start();
  }

  void Run() final { RunAsBroadcaster(); }

  void Stop() final {
    output_.flush();
    output_.close();
    runtime_->Stop();
  }

  void Shutdown() final {
    output_.flush();
    output_.close();
    runtime_->Shutdown();
  }

  SignalHandler GetSignalHandler() final {
    return [this](int) { Shutdown(); };
  }

private:
  struct RunConfig {
    uint32_t num_messages;
  };

  PeerID local_id_;
  HostsTable hosts_table_;
  std::string output_path_;
  std::string config_path_;

  std::unique_ptr<Runtime> runtime_;
  std::ofstream output_;

  RunConfig run_config_;

  static constexpr size_t kLinkPacketLimit = 18000;

  void ParseConfig() {
    std::ifstream ifstream(config_path_);
    ifstream >> run_config_.num_messages;
  }

  static RuntimeConfig GetRuntimeConfig() {
    RuntimeConfig config;
    config.threadpool_workers = 7;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 50;
    return config;
  }

  void RunAsBroadcaster() {
    SpinLock log_mutex;

    auto callback = [this, &log_mutex](PeerID peer, Bytes message) noexcept {
      FIFOMessage application_message;
      application_message.Unmarshal(std::move(message));

      std::lock_guard lock(log_mutex);
      for (size_t payload_idx = 0; payload_idx < FIFOMessage::kNumMessages;
           ++payload_idx) {
        if (application_message.messages[payload_idx] != 0) {
          LOG_DEBUG(
              "FIFO",
              "deliver message with payload = " +
                  std::to_string(application_message.messages[payload_idx]) +
                  " from " + std::to_string(static_cast<uint32_t>(peer)));
          output_ << 'd' << ' ' << static_cast<uint32_t>(peer) << ' '
                  << application_message.messages[payload_idx] << '\n';
        }
      }
    };

    FIFOBroadcast fifo(*runtime_, callback, hosts_table_);
    runtime_->SetTransport(fifo.GetReliableTransportCallback(), hosts_table_,
                           GetReliableRetryPolicy());
    auto &transport = runtime_->GetTransport();
    transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

    UniformReliableBroadcastRateLimiter limiter(
        hosts_table_.GetNetworkSize(/*with_me=*/true), kLinkPacketLimit);

    const auto num_messages = run_config_.num_messages;
    uint32_t current_payload = 1;

    while (current_payload <= num_messages) {
      std::vector<Future<Unit>> futures;

      while (limiter.Fire() && current_payload <= num_messages) {
        // Prepare an application message
        FIFOMessage message;
        {
          std::lock_guard lock(log_mutex);
          for (uint32_t payload_idx = 0;
               payload_idx < FIFOMessage::kNumMessages &&
               current_payload + payload_idx <= num_messages;
               ++payload_idx) {
            auto current_seq_message = current_payload + payload_idx;
            message.messages[payload_idx] = current_seq_message;

            // Write to the log
            output_ << 'b' << ' ' << current_seq_message << '\n';
          }
        }

        current_payload += static_cast<uint32_t>(FIFOMessage::kNumMessages);

        // Prepare a transport message
        auto transport_message = message.Marshal();

        auto future = fifo.Broadcast(std::move(transport_message));
        futures.push_back(std::move(future));
      }
      limiter.Reset();

      auto result = All(std::move(futures)).Get();
      assert(result.HasValue());
    }

    // Use this thread as an event loop runner thread, because we could still
    // need to answer to incoming messages
    runtime_->GetEventLoop().Run();

    transport.Stop();
  }

  ReliableRetryPolicy GetReliableRetryPolicy() {
    auto timer_service_max_timeout = runtime_->GetTimerService().MaxDuration();
    RetryPolicy error_policy(100, 1.5, timer_service_max_timeout);
    RetryPolicy empty_write_policy(100, 1.5, timer_service_max_timeout);
    FairLossRetryPolicy fair_loss_policy(std::move(error_policy),
                                         std::move(empty_write_policy));
    RetryPolicy msg_policy(1000, 3, timer_service_max_timeout);
    ReliableRetryPolicy policy(std::move(msg_policy),
                               std::move(fair_loss_policy));
    return policy;
  }
};
