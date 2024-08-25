#pragma once

#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "applications/application.hpp"
#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "parser.hpp"
#include "primitives/lattice_agreement.hpp"
#include "runtime/runtime.hpp"
#include "util/logging.hpp"

class LatticeAgreementApplication : public IApplication {
public:
  using SignalHandler = IApplication::SignalHandler;

  explicit LatticeAgreementApplication(Parser &parser)
      : local_id_(static_cast<PeerID>(parser.id())),
        hosts_table_(parser.hosts(), local_id_),
        output_path_(parser.outputPath()), config_path_(parser.configPath()) {
    ParseConfigHeader();
    runtime_ = std::make_unique<Runtime>(GetRuntimeConfig());
  }

  void Start() final {
    output_.open(output_path_, std::ios::in | std::ios::out | std::ios::trunc);
    runtime_->Start();
  }

  void Run() final { RunAsProposerAndAcceptor(); }

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
    uint32_t num_proposals;
    uint32_t max_num_elements_in_proposal;
    uint32_t max_num_distinct_elements;
  };

  PeerID local_id_;
  HostsTable hosts_table_;
  std::string output_path_;
  std::string config_path_;

  std::unique_ptr<Runtime> runtime_;
  std::ifstream input_;
  std::ofstream output_;

  RunConfig run_config_;

  static constexpr size_t kLinkPacketLimit = 18000;
  static constexpr uint32_t kMaxNumProposalsInMessage = 8;

  void ParseConfigHeader() {
    input_.open(config_path_, std::ios::in);
    input_ >> run_config_.num_proposals >>
        run_config_.max_num_elements_in_proposal >>
        run_config_.max_num_distinct_elements;

    // Call getline to flush the result of the first reading
    std::string garbage;
    std::getline(input_, garbage);
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

  void RunAsProposerAndAcceptor() {
    RoundID next_round_to_log = 0;
    std::unordered_map<RoundID, ProposalT> out_of_order_cache;

    auto callback = [this, &next_round_to_log, &out_of_order_cache](
                        RoundID round, const ProposalT &decided_set) {
      if (round != next_round_to_log) {
        out_of_order_cache[round] = decided_set;
        return;
      }

      ++next_round_to_log;
      WriteProposalToOutput(decided_set);

      if (out_of_order_cache.empty()) {
        return;
      }

      auto next_it = out_of_order_cache.find(next_round_to_log);
      while (next_it != out_of_order_cache.end()) {
        ++next_round_to_log;
        WriteProposalToOutput(next_it->second);
        out_of_order_cache.erase(next_it);
        next_it = out_of_order_cache.find(next_round_to_log);
      }
    };

    LatticeAgreement la(*runtime_, std::move(callback), hosts_table_);
    runtime_->SetTransport(la.GetReliableTransportCallback(), hosts_table_,
                           GetReliableRetryPolicy());
    auto &transport = runtime_->GetTransport();
    transport.Start(runtime_->GetTimerService(), runtime_->GetEventLoop());

    LatticeAgreementRateLimiter limiter(
        hosts_table_.GetNetworkSize(/*with_me*/ true), kLinkPacketLimit);

    const auto num_proposals = run_config_.num_proposals;
    RoundID current_round = 0;
    while (current_round < num_proposals) {
      std::vector<Future<Unit>> futures;

      while (limiter.Fire() && current_round < num_proposals) {
        ProposalMessage message;
        message.first_round_id = current_round;
        message.proposals.reserve(kMaxNumProposalsInMessage);

        for (uint32_t proposal_idx = 0;
             proposal_idx < kMaxNumProposalsInMessage &&
             current_round + proposal_idx < num_proposals;
             ++proposal_idx) {
          Proposal proposal;
          proposal.proposed_value = ReadProposalFromInput();
          message.proposals.push_back(std::move(proposal));
        }

        auto current_futures = la.Propose(std::move(message));
        current_round += static_cast<RoundID>(current_futures.size());
        for (auto &current_future : current_futures) {
          futures.push_back(std::move(current_future));
        }
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

  void WriteProposalToOutput(const ProposalT &proposal) {
    for (auto value : proposal) {
      output_ << value << ' ';
    }
    output_ << '\n';
  }

  ProposalT ReadProposalFromInput() {
    ProposalT proposal;
    proposal.reserve(run_config_.max_num_elements_in_proposal);
    std::string line;
    std::getline(input_, line);
    std::istringstream iss(line);
    std::copy(std::istream_iterator<uint32_t>(iss),
              std::istream_iterator<uint32_t>(), std::back_inserter(proposal));
    return proposal;
  }
};
