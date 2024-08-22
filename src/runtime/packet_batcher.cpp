#include "runtime/packet_batcher.hpp"
#include "io/transport/hosts.hpp"
#include "io/transport/payload.hpp"
#include "runtime/executor.hpp"
#include "runtime/strand_impl.hpp"

class PacketBatcherImpl : public IStrandImpl<PacketLinked> {
public:
  using Callback = batcher_detail::Callback;

  PacketBatcherImpl(IExecutorPtr underlying, Callback callback)
      : IStrandImpl(underlying), callback_(std::move(callback)) {}

  int64_t ProcessBatch(List batch) override {
    int64_t num_packets = 0;
    while (auto *packet = batch.PopFront()) {
      ++num_packets;
      callback_(packet->source, std::move(packet->message));
      packet->Destroy();
    }

    return num_packets;
  }

private:
  Callback callback_;
};

PacketBatcher::PacketBatcher(IExecutorPtr underlying, Callback callback)
    : impl_(new PacketBatcherImpl(underlying, std::move(callback))) {
  impl_->Refer();
}

PacketBatcher::~PacketBatcher() { impl_->Derefer(); }

void PacketBatcher::AddPacket(PacketLinkedPtr packet) {
  impl_->Process(packet);
}

void PacketBatcher::Task() { impl_->Task(); }
