#include <gtest/gtest.h>

#include "util/instrusive_list.hpp"

#include <vector>

struct SimpleNode : public IntrusiveNode<SimpleNode> {
  SimpleNode(int32_t value) : value(value) {}
  SimpleNode() = default;
  int32_t value{0};
};

TEST(InstrusiveList, SmokeTest) {
  IntrusiveList<SimpleNode> list;

  ASSERT_TRUE(list.Empty());

  SimpleNode node;
  node.value = 1;
  list.PushBack(&node);

  ASSERT_FALSE(list.Empty());
  ASSERT_EQ(1, list.Size());

  auto popped = list.PopFront();

  ASSERT_TRUE(list.Empty());

  ASSERT_EQ(1, popped->value);
}

TEST(InstrusiveList, Insert) {
  IntrusiveList<SimpleNode> list;

  ASSERT_TRUE(list.Empty());

  constexpr size_t num_nodes = 100;
  std::vector<SimpleNode> simple_nodes;
  simple_nodes.reserve(num_nodes);
  for (size_t i = 0; i < num_nodes; ++i) {
    simple_nodes.emplace_back(i);
  }

  for (size_t i = 0; i < num_nodes; ++i) {
    list.PushBack(&simple_nodes[i]);
  }

  ASSERT_FALSE(list.Empty());
  ASSERT_EQ(num_nodes, list.Size());

  for (size_t i = 0; i < num_nodes; ++i) {
    auto popped = list.PopFront();
    ASSERT_EQ(num_nodes - i - 1, list.Size());
    ASSERT_EQ(i, popped->value);
  }

  ASSERT_TRUE(list.Empty());
}

TEST(IntrusiveList, Move) {
  IntrusiveList<SimpleNode> list;

  ASSERT_TRUE(list.Empty());

  constexpr size_t num_nodes = 100;
  std::vector<SimpleNode> simple_nodes;
  simple_nodes.reserve(num_nodes);
  for (size_t i = 0; i < num_nodes; ++i) {
    simple_nodes.emplace_back(i);
  }

  for (size_t i = 0; i < num_nodes; ++i) {
    list.PushBack(&simple_nodes[i]);
  }

  IntrusiveList<SimpleNode> another_list;
  another_list = std::move(list);
  ASSERT_TRUE(list.Empty());

  ASSERT_FALSE(another_list.Empty());
  ASSERT_EQ(num_nodes, another_list.Size());

  for (size_t i = 0; i < num_nodes; ++i) {
    auto popped = another_list.PopFront();
    ASSERT_EQ(num_nodes - i - 1, another_list.Size());
    ASSERT_EQ(i, popped->value);
  }

  ASSERT_TRUE(another_list.Empty());
}
