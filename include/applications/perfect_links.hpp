#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <thread>

#include "application.hpp"
#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/timer_service.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_channel.hpp"
#include "io/transport/reliable_transport.hpp"
#include "parser.hpp"
#include "runtime/runtime.hpp"

using namespace std::chrono_literals;

// To change the format of the message, you just need to implement Marshal and
// Unmarshal
struct PerfectLinksMessage {
  // There is a sequence number on PayloadMessage level, do not use the fact
  // that the message payload is sequence number
  uint32_t seqeunce_number[8];

  Bytes Marshal() const {
    const auto size = sizeof(uint32_t) * 8;
    Bytes bytes(size);
    BinaryMarshalArrayType(seqeunce_number, 8, bytes.data());
    return bytes;
  }

  void Unmarshal(const Bytes &bytes) {
    assert(bytes.size() >= sizeof(uint32_t) * 8);
    BinaryUnmarshalArrayType(seqeunce_number, 8, bytes.data());
  }
};

class PerfectLinks : public IApplication {
public:
  using SignalHandler = IApplication::SignalHandler;
  using DurationMs = TimerService::DurationMs;

  explicit PerfectLinks(Parser &parser)
      : local_id_(static_cast<PeerID>(parser.id())),
        hosts_table_(parser.hosts(), local_id_),
        output_path_(parser.outputPath()), config_path_(parser.configPath()) {
    ParseConfig();
    if (local_id_ == run_config_.receiver_process) {
      runtime_ = std::make_unique<Runtime>(GetRuntimeConfigReceiver());
    } else {
      runtime_ = std::make_unique<Runtime>(GetRuntimeConfigSender());
    }
  }

  void Start() final {
    output_.open(output_path_, std::ios::in | std::ios::out | std::ios::trunc);
    runtime_->Start();
  }

  void Run() final {
    if (run_config_.receiver_process == local_id_) {
      RunAsReceiver();
    } else {
      RunAsSender();
    }
  }

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
    uint32_t receiver_process;
  };

  PeerID local_id_;
  HostsTable hosts_table_;
  std::string output_path_;
  std::string config_path_;

  std::unique_ptr<Runtime> runtime_;
  std::ofstream output_;

  RunConfig run_config_;

  void ParseConfig() {
    std::ifstream ifstream(config_path_);
    ifstream >> run_config_.num_messages >> run_config_.receiver_process;
  }

  static RuntimeConfig GetRuntimeConfigSender() {
    RuntimeConfig config;
    config.threadpool_workers = 7;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 200;
    return config;
  }

  static RuntimeConfig GetRuntimeConfigReceiver() {
    RuntimeConfig config;
    config.threadpool_workers = 7;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 200;
    return config;
  }

  static ReliableRetryPolicy
  GetReliableRetryPolicySender(DurationMs timer_service_max_timeout) {
    RetryPolicy error_policy(100, 1.5, timer_service_max_timeout);
    RetryPolicy empty_write_policy(100, 1.5, timer_service_max_timeout);
    FairLossRetryPolicy fair_loss_policy(std::move(error_policy),
                                         std::move(empty_write_policy));
    RetryPolicy msg_policy(1000, 3, timer_service_max_timeout);
    ReliableRetryPolicy policy(std::move(msg_policy),
                               std::move(fair_loss_policy));
    return policy;
  }

  static ReliableRetryPolicy
  GetReliableRetryPolicyReceiver(DurationMs timer_service_max_timeout) {
    RetryPolicy error_policy(100, 1.5, timer_service_max_timeout);
    RetryPolicy empty_write_policy(100, 1.5, timer_service_max_timeout);
    FairLossRetryPolicy fair_loss_policy(std::move(error_policy),
                                         std::move(empty_write_policy));
    RetryPolicy msg_policy(1000, 3, timer_service_max_timeout);
    ReliableRetryPolicy policy(std::move(msg_policy),
                               std::move(fair_loss_policy));
    return policy;
  }

  void RunAsSender() {
    // Do not care about incoming payload messages
    auto callback = [this](PeerID, Bytes) noexcept {};

    auto timer_service_max_timeout = runtime_->GetTimerService().MaxDuration();
    runtime_->SetTransport(
        std::move(callback), hosts_table_,
        GetReliableRetryPolicySender(timer_service_max_timeout));
    auto &transport = runtime_->GetTransport();

    transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

    auto num_messages = run_config_.num_messages;
    auto receiver = run_config_.receiver_process;

    size_t batch_size = 30000;
    uint32_t current_payload = 1;
    while (current_payload <= num_messages) {
      std::vector<Future<Unit>> acks;
      for (size_t batch_idx = 0;
           batch_idx < batch_size && current_payload <= num_messages;
           ++batch_idx) {
        // Prepare an application message
        PerfectLinksMessage message;
        uint32_t payload[8];
        for (size_t payload_idx = 0; payload_idx < 8; ++payload_idx) {
          message.seqeunce_number[payload_idx] =
              current_payload + static_cast<uint32_t>(payload_idx);

          // Write to the log
          output_ << 'b' << ' '
                  << current_payload + static_cast<uint32_t>(payload_idx)
                  << '\n';
        }
        current_payload += 8;

        // Prepare a transport message
        auto transport_message = message.Marshal();

        auto future = transport.Send(static_cast<PeerID>(receiver),
                                     std::move(transport_message));
        acks.push_back(std::move(future));
      }

      auto ack = All(std::move(acks));
      auto result = ack.Get();
      assert(result.HasValue());
    }

    transport.Stop();
  }

  void RunAsReceiver() {
    auto callback = [this](PeerID src, Bytes message) noexcept {
      PerfectLinksMessage application_message;
      application_message.Unmarshal(std::move(message));

      for (size_t payload_idx = 0; payload_idx < 8; ++payload_idx) {
        output_ << 'd' << ' ' << static_cast<uint32_t>(src) << ' '
                << application_message.seqeunce_number[payload_idx] << '\n';
      }
    };

    auto timer_service_max_timeout = runtime_->GetTimerService().MaxDuration();
    runtime_->SetTransport(
        std::move(callback), hosts_table_,
        GetReliableRetryPolicyReceiver(timer_service_max_timeout));
    auto &transport = runtime_->GetTransport();

    transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

    // Use this thread as an event loop runner thread
    runtime_->GetEventLoop().Run();

    transport.Stop();
  }
};
