#include <gtest/gtest.h>
#include <type_traits>

#include "io/transport/payload.hpp"

enum class Type { kOne, kTwo };

template <typename T> bool MarshalUnmarshalType(T value) {
  Bytes bytes(1024);

  BinaryMarshalPrimitiveType(value, bytes.data());
  auto unmarshalled = BinaryUnmarshalPrimitiveType<T>(bytes.data());

  return value == unmarshalled;
}

TEST(Payload, BinaryMarshalPrimitiveType) {
  uint8_t uint8 = 1;
  int8_t int8 = -1;
  uint16_t uint16 = 300;
  int16_t int16 = -300;
  uint32_t uint32 = 10000000;
  int32_t int32 = -10000000;
  uint64_t uint64 = 100000000;
  int64_t int64 = -100000000;

  Type type = Type::kOne;

  ASSERT_TRUE(MarshalUnmarshalType(uint8));
  ASSERT_TRUE(MarshalUnmarshalType(int8));
  ASSERT_TRUE(MarshalUnmarshalType(uint16));
  ASSERT_TRUE(MarshalUnmarshalType(int16));
  ASSERT_TRUE(MarshalUnmarshalType(uint32));
  ASSERT_TRUE(MarshalUnmarshalType(int32));
  ASSERT_TRUE(MarshalUnmarshalType(uint64));
  ASSERT_TRUE(MarshalUnmarshalType(int64));
  ASSERT_TRUE(MarshalUnmarshalType(type));
}

TEST(Payload, BinaryMarshalArrayType) {
  int32_t array[5] = {1, 2, 3, 4, 5};

  Bytes bytes(1024);
  BinaryMarshalArrayType(array, 5, bytes.data());

  int32_t unmarshalled[5];
  BinaryUnmarshalArrayType(unmarshalled, 5, bytes.data());

  for (size_t i = 0; i < 5; ++i) {
    ASSERT_EQ(array[i], unmarshalled[i]);
  }
}

TEST(Payload, PayloadMessage) {
  std::vector<char> payload{'a', 'b', 'c'};
  Packet message;
  message.id = 1;
  message.message = payload;

  std::vector<char> marshalled(message.Size());
  message.Marshal(marshalled);
  Packet unmarshalled;
  unmarshalled.Unmarshal(marshalled);

  ASSERT_EQ(message.id, unmarshalled.id);
  ASSERT_EQ(message.message, unmarshalled.message);
}

TEST(Payload, AckMessage) {
  AckMessage message;
  message.acks = {1, 2, 3};

  std::vector<char> marshalled(message.Size());
  message.Marshal(marshalled);
  AckMessage unmarshalled;
  unmarshalled.Unmarshal(marshalled);

  ASSERT_EQ(message.acks, unmarshalled.acks);
}

TEST(Payload, Packet) {
  AckMessage message;
  message.acks = {5, 6, 7, 8, 9};

  std::vector<char> marshalled_message(message.Size());
  message.Marshal(marshalled_message);

  Packet packet;
  packet.type = PacketType::kAckMessage;
  packet.message = marshalled_message;

  std::vector<char> marshalled(packet.Size());
  packet.Marshal(marshalled);

  Packet unmarshalled_packet;
  unmarshalled_packet.Unmarshal(marshalled);

  AckMessage unmarshalled_message;
  unmarshalled_message.Unmarshal(unmarshalled_packet.message);

  ASSERT_EQ(packet.type, unmarshalled_packet.type);
  ASSERT_EQ(packet.message, unmarshalled_packet.message);
  ASSERT_EQ(message.acks, unmarshalled_message.acks);
}
