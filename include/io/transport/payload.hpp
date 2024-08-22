#pragma once

#include "io/transport/hosts.hpp"
#include "util/instrusive_list.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using Byte = char;
using BytePtr = Byte *;
using ConstBytePtr = const Byte *;
using Bytes = std::vector<Byte>;
using MessageID = uint32_t;

template <typename T>
BytePtr BinaryMarshalPrimitiveType(T value, BytePtr bytes) {
  auto *value_ptr = reinterpret_cast<BytePtr>(&value);
  std::copy(value_ptr, value_ptr + sizeof(T), bytes);
  return bytes + sizeof(T);
}

template <typename T> T BinaryUnmarshalPrimitiveType(ConstBytePtr bytes) {
  T value;
  auto *value_ptr = reinterpret_cast<BytePtr>(&value);
  std::copy(bytes, bytes + sizeof(T), value_ptr);
  return value;
}

template <typename T>
BytePtr BinaryMarshalArrayType(T *value, size_t size, BytePtr bytes) {
  const auto num_bytes = size * sizeof(T);
  std::memcpy(reinterpret_cast<void *>(bytes),
              reinterpret_cast<const void *>(value), num_bytes);
  return bytes + num_bytes;
}

template <typename T>
void BinaryUnmarshalArrayType(T *value, size_t size, ConstBytePtr bytes) {
  const auto num_bytes = size * sizeof(T);
  std::memcpy(reinterpret_cast<void *>(value),
              reinterpret_cast<const void *>(bytes), num_bytes);
}

struct ISerializable {
  virtual ~ISerializable() = default;

  virtual void Marshal(Bytes &bytes) const = 0;
  virtual void MarshalExtract(Bytes &bytes) = 0;
  virtual void Unmarshal(const Bytes &bytes) = 0;
  virtual size_t Size() const = 0;
  virtual size_t HeaderSize() const = 0;
};

enum class PacketType : uint8_t {
  kAckMessage = 0,
  kPayloadMessage = 1,
};

struct Packet : public ISerializable {
  using IdentifierT = uint8_t;

  // UDP header overhead + IP header estimated overhead
  static constexpr size_t kUDPOverhead = 8 + 20;
  static constexpr size_t kPacketOverhead = sizeof(IdentifierT) +
                                            sizeof(PacketType) +
                                            sizeof(MessageID) + kUDPOverhead;

  // Random header identifier, because wireshark detected some packets as DNS
  // requests
  static constexpr IdentifierT identifier = 67;
  PacketType type{};
  MessageID id{};
  PeerID source{};
  Bytes message{};

  void Marshal(Bytes &bytes) const override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(identifier, data);

    data = BinaryMarshalPrimitiveType(type, data);

    data = BinaryMarshalPrimitiveType(id, data);

    data = BinaryMarshalPrimitiveType(source, data);

    std::copy(message.begin(), message.end(), data);
  }

  void MarshalExtract(Bytes &bytes) override {
    const auto size = Size();
    assert(bytes.size() >= size);
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(identifier, data);

    data = BinaryMarshalPrimitiveType(type, data);

    data = BinaryMarshalPrimitiveType(id, data);

    data = BinaryMarshalPrimitiveType(source, data);

    std::move(message.begin(), message.end(), data);
  }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());
    const auto *data = bytes.data();

    // Skip unique identifier
    data += sizeof(IdentifierT);

    type = BinaryUnmarshalPrimitiveType<PacketType>(data);
    data += sizeof(PacketType);

    id = BinaryUnmarshalPrimitiveType<MessageID>(data);
    data += sizeof(MessageID);

    source = BinaryUnmarshalPrimitiveType<PeerID>(data);
    data += sizeof(PeerID);

    const auto message_size = bytes.size() - HeaderSize();
    message.resize(message_size);
    std::copy(bytes.begin() + static_cast<Bytes::difference_type>(HeaderSize()),
              bytes.end(), message.begin());
  }

  size_t Size() const override { return HeaderSize() + message.size(); }

  size_t HeaderSize() const override {
    return sizeof(IdentifierT) + sizeof(PacketType) + sizeof(MessageID) +
           sizeof(PeerID);
  }
};

struct PacketLinked : public IntrusiveNode<PacketLinked>, public Packet {
  static PacketLinked *Create(Packet packet) {
    return new PacketLinked(std::move(packet));
  }

  void Destroy() { delete this; }

private:
  explicit PacketLinked(Packet packet) : Packet(std::move(packet)) {}
};
using PacketLinkedPtr = PacketLinked *;

struct AckMessage : public ISerializable {
  static constexpr auto kPacketType = PacketType::kAckMessage;

  using SizeT = uint8_t;

  std::vector<MessageID> acks;

  void Marshal(Bytes &bytes) const override {
    assert(acks.size() < std::numeric_limits<SizeT>::max());

    const uint8_t num_elements = static_cast<SizeT>(acks.size());
    const auto size = Size();
    auto *data = bytes.data();

    data = BinaryMarshalPrimitiveType(num_elements, data);
    data = BinaryMarshalArrayType(acks.data(), acks.size(), data);
  }

  void MarshalExtract(Bytes &bytes) override { Marshal(bytes); }

  void Unmarshal(const Bytes &bytes) override {
    assert(bytes.size() >= HeaderSize());
    const auto *data = bytes.data();

    auto num_elements = BinaryUnmarshalPrimitiveType<SizeT>(data);
    data += sizeof(SizeT);

    acks.resize(num_elements);
    BinaryUnmarshalArrayType(acks.data(), num_elements, data);
  }

  size_t Size() const override {
    return sizeof(acks.size()) +
           static_cast<SizeT>(acks.size()) * sizeof(MessageID);
  }

  size_t HeaderSize() const override { return sizeof(SizeT); }
};
