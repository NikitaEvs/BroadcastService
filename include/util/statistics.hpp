#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>

class Statistics {
public:
  using Type = int64_t;
  using AtomicT = std::atomic<int64_t>;

  struct Snapshot {
    // Add type here
    Type backoff_retries;
    Type backoff_deletions;
    Type write_queue_size;
    Type acks_received;
    Type pkts_received;
    Type pkts_sent;
    Type send_budget;
    Type duplicate_pkts;
    Type dropped_pkts;
    Type pending_futures;
    Type deleted_urb_msgs;

    std::string Print() const {
      std::stringstream ss;
      // Add print here
      ss << std::setw(25) << std::left << "Backoff retries:" << backoff_retries
         << "\n";
      ss << std::setw(25) << std::left
         << "Backoff deletions:" << backoff_deletions << "\n";
      ss << std::setw(25) << std::left
         << "Write queue size:" << write_queue_size << "\n";
      ss << std::setw(25) << std::left << "Acks received:" << acks_received
         << "\n";
      ss << std::setw(25) << std::left << "Pkts received:" << pkts_received
         << "\n";
      ss << std::setw(25) << std::left << "Pkts sent:" << pkts_sent << "\n";
      ss << std::setw(25) << std::left << "Send budget:" << send_budget << "\n";
      ss << std::setw(25) << std::left << "Duplicate pkts:" << duplicate_pkts
         << "\n";
      ss << std::setw(25) << std::left << "Dropped pkts:" << dropped_pkts
         << "\n";
      ss << std::setw(25) << std::left << "Pending futures:" << pending_futures
         << "\n";
      ss << std::setw(25) << std::left
         << "Deleted urb msgs:" << deleted_urb_msgs << "\n";
      ss << std::endl;
      return ss.str();
    }
  };

  static Statistics &Instance() {
    static Statistics statistics;
    return statistics;
  }

  void Change(const std::string &name, Type difference) noexcept {
    Mapping(name, [&difference](AtomicT &atomic) noexcept {
      atomic.fetch_add(difference, std::memory_order_relaxed);
    });
  }

  Type Get(const std::string &name) noexcept {
    Type result;
    Mapping(name, [&result](AtomicT &atomic) noexcept {
      result = atomic.load(std::memory_order_relaxed);
    });
    return result;
  }

  Snapshot GetSnapshot() noexcept {
    Snapshot snapshot;
    // Add collection here
    snapshot.backoff_retries = backoff_retries.load(std::memory_order_relaxed);
    snapshot.backoff_deletions =
        backoff_deletions.load(std::memory_order_relaxed);
    snapshot.write_queue_size =
        write_queue_size.load(std::memory_order_relaxed);
    snapshot.acks_received = acks_received.load(std::memory_order_relaxed);
    snapshot.pkts_received = pkts_received.load(std::memory_order_relaxed);
    snapshot.pkts_sent = pkts_sent.load(std::memory_order_relaxed);
    snapshot.send_budget = send_budget.load(std::memory_order_relaxed);
    snapshot.duplicate_pkts = duplicate_pkts.load(std::memory_order_relaxed);
    snapshot.dropped_pkts = dropped_pkts.load(std::memory_order_relaxed);
    snapshot.pending_futures = pending_futures.load(std::memory_order_relaxed);
    snapshot.deleted_urb_msgs =
        deleted_urb_msgs.load(std::memory_order_relaxed);

    return snapshot;
  }

private:
  using Operation = std::function<void(std::atomic<Type> &)>;

  // Add atomic here
  AtomicT backoff_retries{0};
  AtomicT backoff_deletions{0};
  AtomicT write_queue_size{0};
  AtomicT acks_received{0};
  AtomicT pkts_received{0};
  AtomicT pkts_sent{0};
  AtomicT send_budget{0};
  AtomicT duplicate_pkts{0};
  AtomicT dropped_pkts{0};
  AtomicT pending_futures{0};
  AtomicT deleted_urb_msgs{0};

  Statistics() = default;

  // Add mapping here
  void Mapping(const std::string &name, const Operation &op) {
    if (name == "backoff_retries") {
      op(backoff_retries);
    } else if (name == "backoff_deletions") {
      op(backoff_deletions);
    } else if (name == "write_queue_size") {
      op(write_queue_size);
    } else if (name == "acks_received") {
      op(acks_received);
    } else if (name == "pkts_received") {
      op(pkts_received);
    } else if (name == "pkts_sent") {
      op(pkts_sent);
    } else if (name == "send_budget") {
      op(send_budget);
    } else if (name == "duplicate_pkts") {
      op(duplicate_pkts);
    } else if (name == "dropped_pkts") {
      op(dropped_pkts);
    } else if (name == "pending_futures") {
      op(pending_futures);
    } else if (name == "deleted_urb_msgs") {
      op(deleted_urb_msgs);
    } else {
      assert(false && "Statistics::Mapping is called on the invalid name");
    }
  }
};
