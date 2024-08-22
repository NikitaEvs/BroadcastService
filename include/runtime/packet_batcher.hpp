#pragma once

#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "runtime/executor.hpp"
#include "util/nonmovable.hpp"

namespace batcher_detail {
using Callback = std::function<void(PeerID, Bytes)>;
}

class PacketBatcherImpl;

class PacketBatcher : public ITask, private NonMoveable {
public:
  using Impl = PacketBatcherImpl;
  using ImplPtr = PacketBatcherImpl *;
  using Callback = batcher_detail::Callback;

  PacketBatcher(IExecutorPtr underlying, Callback callback);
  ~PacketBatcher();

  void AddPacket(PacketLinkedPtr packet);

  void Task() override;

private:
  ImplPtr impl_;
};
