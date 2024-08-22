#pragma once

#include <chrono>
#include <cstdint>
#include <set>

#include "concurrency/contract.hpp"
#include "concurrency/future.hpp"
#include "concurrency/promise.hpp"
#include "concurrency/spinlock.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "io/transport/reliable_transport.hpp"
#include "runtime/runtime.hpp"
#include "util/logging.hpp"
#include "util/set_operation.hpp"

using RoundID = uint32_t;
using ProposeValueT = uint32_t;
using ProposalT = std::vector<ProposeValueT>;

enum class LAMessageType : uint8_t {
  kProposal = 0,
  kAccept = 1,
  kDecide = 2,
};

class LAMessage : public ISerializable {
public:
  LAMessageType type{};
  Bytes message{};

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);

    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(type, data);

    std::copy(message.begin(), message.end(), data);
  }

  void MarshalExtract(Bytes &bytes) override {
    const auto size = Size();
    assert(bytes.size() >= size);

    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(type, data);

    std::move(message.begin(), message.end(), data);
  }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());

    const auto *data = bytes.data();

    type = BinaryUnmarshalPrimitiveType<LAMessageType>(data);
    data += sizeof(LAMessageType);

    const auto message_size = bytes.size() - HeaderSize();
    message.resize(message_size);
    std::copy(bytes.begin() + static_cast<Bytes::difference_type>(HeaderSize()),
              bytes.end(), message.begin());
  }

  size_t Size() const override { return HeaderSize() + message.size(); }

  size_t HeaderSize() const override { return sizeof(LAMessageType); }
};

struct Proposal {
  using SizeT = uint32_t;

  ProposalT proposed_value;
  uint32_t active_proposal_number = 0;
};

class ProposalMessage : public ISerializable {
public:
  using SizeT = uint8_t;

  // RoundID of the first element in the proposals array
  RoundID first_round_id;
  std::vector<Proposal> proposals;

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(first_round_id, data);

    const auto num_proposals = static_cast<SizeT>(proposals.size());
    data = BinaryMarshalPrimitiveType(num_proposals, data);

    for (const auto &proposal : proposals) {
      const auto num_elements =
          static_cast<Proposal::SizeT>(proposal.proposed_value.size());
      data = BinaryMarshalPrimitiveType(num_elements, data);

      std::vector<ProposeValueT> proposal_buffer;
      proposal_buffer.reserve(num_elements);
      proposal_buffer.insert(proposal_buffer.end(),
                             proposal.proposed_value.begin(),
                             proposal.proposed_value.end());
      data = BinaryMarshalArrayType(proposal_buffer.data(),
                                    proposal_buffer.size(), data);

      data = BinaryMarshalPrimitiveType(proposal.active_proposal_number, data);
    }
  }

  void MarshalExtract(Bytes &bytes) override { Marshal(bytes); }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());
    const auto *data = bytes.data();

    first_round_id = BinaryUnmarshalPrimitiveType<RoundID>(data);
    data += sizeof(RoundID);

    const auto num_proposals = BinaryUnmarshalPrimitiveType<SizeT>(data);
    data += sizeof(SizeT);

    proposals.resize(num_proposals);
    for (auto &proposal : proposals) {
      const auto num_elements =
          BinaryUnmarshalPrimitiveType<Proposal::SizeT>(data);
      data += sizeof(Proposal::SizeT);

      proposal.proposed_value.resize(num_elements);
      BinaryUnmarshalArrayType(proposal.proposed_value.data(), num_elements,
                               data);
      data += sizeof(ProposeValueT) * num_elements;

      proposal.active_proposal_number =
          BinaryUnmarshalPrimitiveType<uint32_t>(data);
      data += sizeof(uint32_t);
    }
  }

  size_t Size() const override {
    size_t size = 0;
    size += HeaderSize();

    for (const auto &proposal : proposals) {
      size += sizeof(Proposal::SizeT) +
              sizeof(ProposeValueT) * proposal.proposed_value.size() +
              sizeof(uint32_t);
    }

    return size;
  }

  size_t HeaderSize() const override { return sizeof(RoundID) + sizeof(SizeT); }
};

enum class AcceptStatus : uint8_t {
  kAck = 0,
  kNack = 1,
};

struct Accept {
  using SizeT = uint32_t;

  AcceptStatus status;
  uint32_t proposal_number;
  ProposalT accepted_value; // optional field
};

class AcceptMessage : public ISerializable {
public:
  using SizeT = uint8_t;

  // RoundID of the first element in the proposals array
  RoundID first_round_id;
  std::vector<Accept> accepts;

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);

    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(first_round_id, data);

    const auto num_accepts = static_cast<SizeT>(accepts.size());
    data = BinaryMarshalPrimitiveType(num_accepts, data);

    for (const auto &accept : accepts) {
      data = BinaryMarshalPrimitiveType(accept.status, data);
      data = BinaryMarshalPrimitiveType(accept.proposal_number, data);

      if (accept.status == AcceptStatus::kNack) {
        const auto accepted_value_size =
            static_cast<Accept::SizeT>(accept.accepted_value.size());
        data = BinaryMarshalPrimitiveType(accepted_value_size, data);

        std::vector<ProposeValueT> accepted_buffer;
        accepted_buffer.reserve(accept.accepted_value.size());
        accepted_buffer.insert(accepted_buffer.end(),
                               accept.accepted_value.begin(),
                               accept.accepted_value.end());
        data = BinaryMarshalArrayType(accepted_buffer.data(),
                                      accepted_value_size, data);
      }
    }
  }

  void MarshalExtract(Bytes &bytes) override { Marshal(bytes); }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());

    const auto *data = bytes.data();

    first_round_id = BinaryUnmarshalPrimitiveType<RoundID>(data);
    data += sizeof(RoundID);

    const auto num_accepts = BinaryUnmarshalPrimitiveType<SizeT>(data);
    data += sizeof(SizeT);

    accepts.resize(num_accepts);

    for (auto &accept : accepts) {
      accept.status =
          BinaryUnmarshalPrimitiveType<decltype(accept.status)>(data);
      data += sizeof(decltype(accept.status));
      accept.proposal_number =
          BinaryUnmarshalPrimitiveType<decltype(accept.proposal_number)>(data);
      data += sizeof(decltype(accept.proposal_number));

      if (accept.status == AcceptStatus::kNack) {
        const auto accepted_value_size =
            BinaryUnmarshalPrimitiveType<Accept::SizeT>(data);
        data += sizeof(Accept::SizeT);

        accept.accepted_value.resize(accepted_value_size);
        BinaryUnmarshalArrayType(accept.accepted_value.data(),
                                 accepted_value_size, data);
        data += sizeof(ProposeValueT) * accepted_value_size;
        accept.accepted_value.reserve(accepted_value_size);
      }
    }
  }

  size_t Size() const override {
    size_t size = 0;
    size += HeaderSize();

    for (const auto &accept : accepts) {
      size += sizeof(AcceptStatus) + sizeof(uint32_t);
      if (accept.status == AcceptStatus::kNack) {
        size += sizeof(Accept::SizeT) +
                sizeof(ProposeValueT) * accept.accepted_value.size();
      }
    }

    return size;
  }

  size_t HeaderSize() const override { return sizeof(RoundID) + sizeof(SizeT); }
};

class DecideMessage : public ISerializable {
public:
  RoundID round_id;

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(round_id, data);
  }

  void MarshalExtract(Bytes &bytes) override { Marshal(bytes); }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());
    const auto *data = bytes.data();

    round_id = BinaryUnmarshalPrimitiveType<RoundID>(data);
    data += sizeof(RoundID);
  }

  size_t Size() const override { return HeaderSize(); }

  size_t HeaderSize() const override { return sizeof(RoundID); }
};

struct RoundState {
  using PromiseT = Promise<Unit>;
  using FutureT = Future<Unit>;

  uint32_t active_proposal_number{0};
  uint32_t acks{0};
  uint32_t nacks{0};
  uint32_t decisions{0};
  ProposalT proposed_value;
  ProposalT accepted_value;
  bool completed{false};
  PromiseT promise;
  std::optional<FutureT> preliminary_future;

  RoundState(const ProposalT &proposed_value, PromiseT promise)
      : proposed_value(proposed_value), promise(std::move(promise)) {}
};

class LatticeAgreement {
public:
  using ReliableTransportCallback = ReliableTransport::Callback;
  using Callback = std::function<void(RoundID, const ProposalT &)>;
  using FutureT = Future<Unit>;
  using FuturesT = std::vector<FutureT>;
  using Mutex = SpinLock;

  LatticeAgreement(Runtime &runtime, Callback callback, const HostsTable &hosts)
      : runtime_(runtime), callback_(std::move(callback)),
        localID_(hosts.LocalID()),
        network_size_(hosts.GetNetworkSize(/*with_me=*/true)) {}

  FuturesT Propose(ProposalMessage proposal_message) {
    FuturesT futures;

    LOG_DEBUG("LATTIC",
              "propose " + std::to_string(proposal_message.proposals.size()) +
                  " proposals starting with round_id = " +
                  std::to_string(proposal_message.first_round_id) + " from " +
                  std::to_string(static_cast<uint32_t>(localID_)));

    {
      std::lock_guard lock(round_states_mutex_);

      RoundID current_round_id = proposal_message.first_round_id;
      for (auto &proposal : proposal_message.proposals) {
        auto f = InitializeNextRoundState(current_round_id,
                                          proposal.proposed_value, lock);
        futures.push_back(std::move(f));
        ++current_round_id;
        Statistics::Instance().Change("pending_futures", 1);
      }
    }

    auto la_message = WrapProposalMessage(std::move(proposal_message));
    Bytes message(la_message.Size());
    la_message.MarshalExtract(message);
    BestEffortBroadcast(std::move(message));

    return futures;
  }

  ReliableTransportCallback GetReliableTransportCallback() {
    return [this](PeerID peer, Bytes message) noexcept {
      LAMessage la_message;
      la_message.Unmarshal(message);

      if (la_message.type == LAMessageType::kProposal) {
        ProposalMessage proposal_message;
        proposal_message.Unmarshal(la_message.message);
        HandleProposalMessage(peer, std::move(proposal_message));
      } else if (la_message.type == LAMessageType::kAccept) {
        AcceptMessage accept_message;
        accept_message.Unmarshal(la_message.message);
        HandleAcceptMessage(peer, std::move(accept_message));
      } else if (la_message.type == LAMessageType::kDecide) {
        DecideMessage decide_message;
        decide_message.Unmarshal(la_message.message);

      } else {
        LOG_ERROR("LATTIC", "invalid LAMessage type");
      }
    };
  }

private:
  Runtime &runtime_;
  Callback callback_;
  PeerID localID_;
  size_t network_size_;

  Mutex round_states_mutex_;
  std::vector<std::unique_ptr<RoundState>>
      round_states_; // guarded by round_states_mutex_

  void InitializeEmptyRoundStates(RoundID round, std::lock_guard<Mutex> &) {
    ProposalT empty_proposal;

    while (round >= round_states_.size()) {
      auto [f, p] = MakeContract<Unit>();
      round_states_.emplace_back(
          std::make_unique<RoundState>(empty_proposal, std::move(p)));
      round_states_.back()->preliminary_future.emplace(std::move(f));
    }
  }

  FutureT InitializeNextRoundState(RoundID round, const ProposalT &proposal,
                                   std::lock_guard<Mutex> &) {
    // Already created round structure
    if (round < round_states_.size()) {
      auto &state = *round_states_[round].get();
      state.proposed_value = proposal;
      assert(state.preliminary_future.has_value() &&
             "preliminary contract was created without a future");
      return std::move(*state.preliminary_future);
    }

    assert(round == round_states_.size() && "round id is invalid");
    auto [f, p] = MakeContract<Unit>();
    round_states_.emplace_back(
        std::make_unique<RoundState>(proposal, std::move(p)));
    return std::move(f);
  }

  RoundState &GetRoundState(RoundID round) {
    std::lock_guard lock(round_states_mutex_);
    if (round >= round_states_.size()) {
      InitializeEmptyRoundStates(round, lock);
    }
    assert(round < round_states_.size() && "round id is invalid");
    return *round_states_[round].get();
  }

  LAMessage WrapMessage(ISerializable *input_message, LAMessageType type) {
    LAMessage output_message;
    output_message.type = type;
    output_message.message.resize(input_message->Size());
    input_message->MarshalExtract(output_message.message);
    return output_message;
  }

  LAMessage WrapProposalMessage(ProposalMessage input_message) {
    LAMessage output_message;
    output_message.type = LAMessageType::kProposal;
    output_message.message.resize(input_message.Size());
    input_message.MarshalExtract(output_message.message);
    return output_message;
  }

  LAMessage WrapAcceptMessage(AcceptMessage input_message) {
    LAMessage output_message;
    output_message.type = LAMessageType::kAccept;
    output_message.message.resize(input_message.Size());
    input_message.MarshalExtract(output_message.message);
    return output_message;
  }

  LAMessage WrapDecideMessage(DecideMessage input_message) {
    return WrapMessage(&input_message, LAMessageType::kDecide);
  }

  void BestEffortBroadcast(Bytes message) {
    auto peers = runtime_.GetHosts().GetPeers();
    auto localID = runtime_.GetHosts().LocalID();
    auto &transport = runtime_.GetTransport();

    LOG_DEBUG("LATTIC", "broadcast message from " +
                            std::to_string(static_cast<uint32_t>(localID_)));

    for (const auto &peer : peers) {
      transport.Send(peer, message).Detach();
    }

    transport.Send(localID, std::move(message)).Detach();
  }

  void HandleProposalMessage(PeerID src, ProposalMessage message) {
    RoundID current_round_id = message.first_round_id;
    AcceptMessage accept_message;
    accept_message.first_round_id = current_round_id;

    LOG_DEBUG("LATTIC", "handle proposal message from " +
                            std::to_string(static_cast<uint32_t>(src)) +
                            " with round_id starting from " +
                            std::to_string(current_round_id));

    for (auto &proposal : message.proposals) {
      auto accept = HandlePropose(src, current_round_id, std::move(proposal));
      accept_message.accepts.push_back(std::move(accept));
      ++current_round_id;
    }

    auto la_message = WrapAcceptMessage(std::move(accept_message));
    Bytes buffer(la_message.Size());
    la_message.MarshalExtract(buffer);
    auto &transport = runtime_.GetTransport();
    transport.Send(src, std::move(buffer)).Detach();
  }

  void HandleAcceptMessage(PeerID src, AcceptMessage message) {
    RoundID current_round_id = message.first_round_id;

    LOG_DEBUG("LATTIC", "handle accept message from " +
                            std::to_string(static_cast<uint32_t>(src)) +
                            " with round_id starting from " +
                            std::to_string(current_round_id));

    for (auto &accept : message.accepts) {
      switch (accept.status) {
      case AcceptStatus::kAck:
        HandleAck(src, current_round_id, std::move(accept));
        break;
      case AcceptStatus::kNack:
        HandleNack(src, current_round_id, std::move(accept));
        break;
      default:
        break;
      }
      ++current_round_id;
    }
  }

  void HandleDecideMessage(PeerID /*src*/, DecideMessage message) {
    RoundState &state = GetRoundState(message.round_id);

    ++state.decisions;

    if (state.decisions == network_size_) {
      state.proposed_value.clear();
      state.accepted_value.clear();
    }
  }

  Accept HandlePropose(PeerID /*src*/, RoundID round, Proposal proposal) {
    RoundState &state = GetRoundState(round);

    Accept accept;

    VectorSetUnion(state.accepted_value, proposal.proposed_value);
    if (state.accepted_value.size() == proposal.proposed_value.size()) {
      LOG_DEBUG(
          "LATTIC",
          "handle propose for round = " + std::to_string(round) +
              ", accepted_value <= proposed_value, reset it and send ack");

      state.accepted_value = std::move(proposal.proposed_value);
      accept.status = AcceptStatus::kAck;
      accept.proposal_number = proposal.active_proposal_number;
    } else {
      LOG_DEBUG(
          "LATTIC",
          "handle propose for round = " + std::to_string(round) +
              ", accepted_value > proposed_value, union it and send nack");

      accept.status = AcceptStatus::kNack;
      accept.proposal_number = proposal.active_proposal_number;
      accept.accepted_value = state.accepted_value;
    }

    return accept;
  }

  void HandleAck(PeerID src, RoundID round, Accept accept) {
    RoundState &state = GetRoundState(round);

    if (state.completed) {
      LOG_DEBUG("LATTIC", "handle ack for round = " + std::to_string(round) +
                              ", round is completed, do nothing");
      return;
    }

    if (state.active_proposal_number == accept.proposal_number) {
      LOG_DEBUG("LATTIC",
                "handle ack for round = " + std::to_string(round) +
                    ", round is incomplete, active proposal number = " +
                    std::to_string(state.active_proposal_number) +
                    " equals to accept proposal number, update acks");

      ++state.acks;
      CheckAckNackConditions(src, round, state);
    } else {
      LOG_DEBUG("LATTIC",
                "handle ack for round = " + std::to_string(round) +
                    ", round is incomplete, active proposal number = " +
                    std::to_string(state.active_proposal_number) +
                    " != " + std::to_string(accept.proposal_number) +
                    " = accept proposal number, do not count as ack");
    }
  }

  void HandleNack(PeerID src, RoundID round, Accept accept) {
    RoundState &state = GetRoundState(round);

    if (state.completed) {
      LOG_DEBUG("LATTIC", "handle nack for round = " + std::to_string(round) +
                              ", round is completed, do nothing");
      return;
    }

    if (state.active_proposal_number == accept.proposal_number) {
      LOG_DEBUG("LATTIC",
                "handle nack for round = " + std::to_string(round) +
                    ", round is incomplete, active proposal number = " +
                    std::to_string(state.active_proposal_number) +
                    " equals to accept proposal number, update nacks, union "
                    "proposals");
      ++state.nacks;
      VectorSetUnion(state.proposed_value, accept.accepted_value);
      // SetUnion(state.proposed_value, accept.accepted_value);
      CheckAckNackConditions(src, round, state);
    } else {
      LOG_DEBUG("LATTIC",
                "handle nack for round = " + std::to_string(round) +
                    ", round is incomplete, active proposal number = " +
                    std::to_string(state.active_proposal_number) +
                    " != " + std::to_string(accept.proposal_number) +
                    " = accept proposal number, do not count as nack");
    }
  }

  void CheckAckNackConditions(PeerID /*src*/, RoundID round,
                              RoundState &state) {
    auto majority = Majority();
    if (state.acks >= majority) {
      LOG_DEBUG("LATTIC", "collected " + std::to_string(state.acks) +
                              " >= majority = " + std::to_string(majority) +
                              ", decide proposed value");
      Decide(round, state);
    } else if (state.nacks > 0 && (state.acks + state.nacks >= majority)) {
      // } else if (state.nacks > 0) {
      LOG_DEBUG("LATTIC",
                "collected " + std::to_string(state.acks) + " acks, " +
                    std::to_string(state.nacks) +
                    " nacks, >= majority = " + std::to_string(majority) +
                    ", move to next proposal value " +
                    std::to_string(state.active_proposal_number + 1));
      MoveToNextProposalNumber(round, state);
    }
  }

  size_t Majority() const { return network_size_ / 2 + 1; }

  void Decide(RoundID round, RoundState &state) {
    state.completed = true;
    Statistics::Instance().Change("pending_futures", -1);
    std::move(state.promise).SetValue(Unit{});
    callback_(round, state.proposed_value);
  }

  void MoveToNextProposalNumber(RoundID round, RoundState &state) {
    ++state.active_proposal_number;
    state.acks = 0;
    state.nacks = 0;

    ProposalMessage proposal_message;
    proposal_message.first_round_id = round;
    Proposal proposal{state.proposed_value, state.active_proposal_number};
    proposal_message.proposals.push_back(std::move(proposal));

    auto la_message = WrapProposalMessage(std::move(proposal_message));
    Bytes message(la_message.Size());
    la_message.MarshalExtract(message);
    BestEffortBroadcast(std::move(message));
  }
};

// TODO: Deduplicate code with UniformReliableBroadcastRateLimiter
class LatticeAgreementRateLimiter {
public:
  LatticeAgreementRateLimiter(size_t network_size, size_t link_packet_limit) {
    // Max number of packets generated by one lattice agreement
    // TODO: Recalculate it
    size_t packet_multiplier = 2 * network_size * network_size;

    batch_size_ =
        std::max(static_cast<size_t>(1), link_packet_limit / packet_multiplier);
  }

  bool Fire() {
    if (current_fire_ == batch_size_) {
      return false;
    }
    ++current_fire_;
    return true;
  }

  void Reset() { current_fire_ = 0; }

private:
  size_t batch_size_;
  size_t current_fire_{0};
};
