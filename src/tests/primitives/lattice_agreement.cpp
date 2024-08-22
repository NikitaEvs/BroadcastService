#include <gtest/gtest.h>
#include <thread>

#include "concurrency/combine.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "parser.hpp"
#include "primitives/lattice_agreement.hpp"
#include "runtime/runtime.hpp"
#include "util/random_util.hpp"
#include "util/set_operation.hpp"

using namespace std::chrono_literals;

class LatticeAgreementTest : public testing::Test {
protected:
  using Host = Parser::Host;

  void SetUp() override {
    runtime_ = std::make_unique<Runtime>(GetRuntimeConfig());
    runtime_->Start();
  }

public:
  RuntimeConfig GetRuntimeConfig() {
    RuntimeConfig config;
    config.threadpool_workers = 8;
    config.eventloop_runners = 4;
    config.eventloop_max_num_events = 100;
    config.timerservice_tick_duration = 100;
    config.max_send_budget = 50;
    return config;
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

  void SetupHosts(PeerID localID) {
    std::vector<Host> host_list;

    std::string localhost = "localhost";
    host_list.emplace_back(0, localhost, 9000);
    host_list.emplace_back(1, localhost, 9001);
    host_list.emplace_back(2, localhost, 9002);
    host_list.emplace_back(3, localhost, 9003);
    hosts_ = HostsTable(std::move(host_list), localID);
  }

  std::unique_ptr<Runtime> runtime_;
  HostsTable hosts_;
};

// Declaration for the build flag "missing-declarations"
ProposalMessage CreateProposalMessage(RoundID round_id,
                                      const std::vector<ProposalT> &proposals);
ProposalMessage CreateProposalMessage(RoundID round_id,
                                      const std::vector<ProposalT> &proposals) {
  ProposalMessage message;
  message.first_round_id = round_id;

  for (const auto &proposal : proposals) {
    message.proposals.push_back({proposal, 0});
  }

  return message;
}

// Declaration for the build flag "missing-declarations"
ProposalMessage CreateRandomProposalMessage(RoundID round_id, size_t num_rounds,
                                            size_t proposal_size);
ProposalMessage CreateRandomProposalMessage(RoundID round_id, size_t num_rounds,
                                            size_t proposal_size) {
  std::vector<std::unordered_set<ProposeValueT>> proposals_sets(num_rounds);
  for (auto &proposal_set : proposals_sets) {
    for (size_t valud_idx = 0; valud_idx < proposal_size; ++valud_idx) {
      proposal_set.insert(RandInt<uint32_t>(0, 100));
    }
  }

  std::vector<ProposalT> proposals;
  proposals.reserve(num_rounds);
  for (auto &proposal_set : proposals_sets) {
    ProposalT proposal;
    for (auto item : proposal_set) {
      proposal.push_back(item);
      // proposal.insert(item);
    }
    proposals.push_back(std::move(proposal));
  }

  return CreateProposalMessage(round_id, proposals);
}

// Declaration for the build flag "missing-declarations"
void SmokeTest(PeerID localID, LatticeAgreementTest &test);
void SmokeTest(PeerID localID, LatticeAgreementTest &test) {
  test.SetupHosts(localID);

  auto callback = [](RoundID round, const ProposalT &decided_set) {
    std::cout << "Round: " << round << std::endl;
    std::cout << "Decided set: ";
    for (auto value : decided_set) {
      std::cout << value << " ";
    }
    std::cout << std::endl;
  };

  LatticeAgreement la(*test.runtime_, callback, test.hosts_);
  test.runtime_->SetTransport(la.GetReliableTransportCallback(), test.hosts_,
                              test.GetReliableRetryPolicy());
  auto &transport = test.runtime_->GetTransport();
  transport.Start(test.runtime_->GetTimerService(),
                  test.runtime_->GetEventLoop());

  RuntimeScopeGuard guard(*test.runtime_);

  std::vector<ProposalT> proposals = {{1, 2, 3}};
  auto message = CreateProposalMessage(0, proposals);

  auto futures = la.Propose(std::move(message));
  ASSERT_EQ(1, futures.size());
  auto result = futures.front().Get();
  ASSERT_TRUE(result.HasValue());

  std::this_thread::sleep_for(1s);
}

// Condition: run SmokeTest1, SmokeTest2, SmokeTest3 simultaniously
TEST_F(LatticeAgreementTest, SmokeTest1) { SmokeTest(0, *this); }
TEST_F(LatticeAgreementTest, SmokeTest2) { SmokeTest(1, *this); }
TEST_F(LatticeAgreementTest, SmokeTest3) { SmokeTest(2, *this); }

// Declaration for the build flag "missing-declarations"
bool PartiallyValidateDecision(const ProposalT &proposal,
                               const ProposalT &decision);
bool PartiallyValidateDecision(const ProposalT &proposal,
                               const ProposalT &decision) {
  return VectorSetLessOrEquals(proposal, decision);
}

// Declaration for the build flag "missing-declarations"
bool PartiallyValidateDecisions(const std::vector<ProposalT> &proposals,
                                const std::vector<ProposalT> &decisions);
bool PartiallyValidateDecisions(const std::vector<ProposalT> &proposals,
                                const std::vector<ProposalT> &decisions) {
  if (proposals.size() != decisions.size()) {
    return false;
  }

  for (size_t idx = 0; idx < proposals.size(); ++idx) {
    if (!PartiallyValidateDecision(proposals[idx], decisions[idx])) {
      return false;
    }
  }

  return true;
}

// Declaration for the build flag "missing-declarations"
void LoadTest(PeerID localID, LatticeAgreementTest &test);
void LoadTest(PeerID localID, LatticeAgreementTest &test) {
  test.SetupHosts(localID);

  std::atomic<bool> stop_stats{false};
  test.runtime_->GetThreadPool().Execute(Lambda::Create([&stop_stats]() {
    while (!stop_stats.load(std::memory_order_relaxed)) {
      auto snapshot = Statistics::Instance().GetSnapshot();
      std::cout << snapshot.Print();
      std::this_thread::sleep_for(10ms);
    }
  }));

  constexpr PeerID num_hosts = 4;
  constexpr RoundID num_rounds = 100000;
  constexpr size_t proposal_size = 30;
  constexpr RoundID num_proposals_in_message = 8;
  constexpr size_t link_packet_limit = 18000;

  SpinLock decided_values_mutex;
  std::vector<ProposalT> decided_values(num_rounds);

  std::vector<ProposalMessage> proposed_values;

  for (RoundID round_idx = 0; round_idx < num_rounds;
       round_idx += num_proposals_in_message) {
    proposed_values.push_back(CreateRandomProposalMessage(
        round_idx, std::min(num_rounds - round_idx, num_proposals_in_message),
        proposal_size));
  }

  auto callback = [&decided_values_mutex, &decided_values](
                      RoundID round, const ProposalT &decided_set) {
    std::lock_guard lock(decided_values_mutex);
    decided_values[round] = decided_set;
  };

  LatticeAgreement la(*test.runtime_, callback, test.hosts_);
  test.runtime_->SetTransport(la.GetReliableTransportCallback(), test.hosts_,
                              test.GetReliableRetryPolicy());
  auto &transport = test.runtime_->GetTransport();
  transport.Start(test.runtime_->GetTimerService(),
                  test.runtime_->GetEventLoop());

  RuntimeScopeGuard guard(*test.runtime_);

  auto start = std::chrono::high_resolution_clock::now();

  LatticeAgreementRateLimiter limiter(num_hosts, link_packet_limit);

  RoundID current_round = 0;
  size_t proposed_values_idx = 0;
  while (current_round < num_rounds) {
    std::vector<Future<Unit>> futures;

    while (limiter.Fire() && current_round < num_rounds) {
      auto current_futures = la.Propose(proposed_values[proposed_values_idx]);
      current_round += static_cast<RoundID>(current_futures.size());
      ++proposed_values_idx;
      for (auto &current_future : current_futures) {
        futures.push_back(std::move(current_future));
      }
    }
    limiter.Reset();

    auto result = All(std::move(futures)).Get();
    ASSERT_TRUE(result.HasValue());
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::this_thread::sleep_for(10s);

  stop_stats.store(true, std::memory_order_relaxed);

  std::lock_guard lock(decided_values_mutex);

  std::vector<ProposalT> proposals;
  for (const auto &proposed_value : proposed_values) {
    for (const auto &proposal : proposed_value.proposals) {
      proposals.push_back(proposal.proposed_value);
    }
  }

  ASSERT_TRUE(PartiallyValidateDecisions(proposals, decided_values));

  std::cout << "Proposed " << num_rounds << " proposals in " << duration.count()
            << " ms" << std::endl;
  double speed = (static_cast<double>(60 * 1000) /
                  static_cast<double>(duration.count()) * num_rounds);
  std::cout << std::fixed << std::showpoint;
  std::cout << std::setprecision(2);
  std::cout << "Speed: " << speed << " proposals in a minute" << std::endl;

  // std::cout << "Proposals: " << std::endl;
  // RoundID round_id = 0;
  // for (const auto &proposal : proposals) {
  //   std::cout << round_id << ": [";
  //   for (auto value : proposal) {
  //     std::cout << value << " ";
  //   }
  //   std::cout << "]" << std::endl;
  //   ++round_id;
  // }

  // std::cout << "Decisions: " << std::endl;
  // round_id = 0;
  // for (const auto &decision : decided_values) {
  //   std::cout << round_id << ": [";
  //   for (auto value : decision) {
  //     std::cout << value << " ";
  //   }
  //   std::cout << "]" << std::endl;
  //   ++round_id;
  // }
}

// Condition: run LoadTest1, LoadTest2, LoadTest3, LoadTest4 simultaniously
TEST_F(LatticeAgreementTest, LoadTest1) { LoadTest(0, *this); }
TEST_F(LatticeAgreementTest, LoadTest2) { LoadTest(1, *this); }
TEST_F(LatticeAgreementTest, LoadTest3) { LoadTest(2, *this); }
TEST_F(LatticeAgreementTest, LoadTest4) { LoadTest(3, *this); }
