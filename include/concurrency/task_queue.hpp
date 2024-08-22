#pragma once

#include <atomic>

#include "runtime/task.hpp"
#include "util/instrusive_list.hpp"

// Modified https://en.wikipedia.org/wiki/Treiber_stack
template <typename T> class BatchedLFStack {
public:
  using Task = T;
  using TaskPtr = T *;
  using Node = IntrusiveNode<Task>;
  using List = IntrusiveList<Task>;

  void Push(TaskPtr task) {
    auto *current_top = top_.load();
    task->next_ = PtrToNode(current_top);

    while (!top_.compare_exchange_weak(current_top, TaskToPtr(task))) {
      task->next_ = PtrToNode(current_top);
    }
  }

  void Push(List list) {
    if (list.Empty()) {
      return;
    }

    auto *current_top = top_.load();
    list.Tail()->next_ = PtrToNode(current_top);

    while (!top_.compare_exchange_weak(current_top, NodeToPtr(list.Head()))) {
      list.Tail()->next_ = PtrToNode(current_top);
    }
  }

  Node *TryPopAll() {
    auto *current_top = top_.load();

    while (!top_.compare_exchange_weak(current_top, nullptr)) {
    }

    return PtrToNode(current_top);
  }

private:
  std::atomic<void *> top_{nullptr};

  void *TaskToPtr(Task *task) { return NodeToPtr(static_cast<Node *>(task)); }

  Node *PtrToNode(void *ptr) { return reinterpret_cast<Node *>(ptr); }

  void *NodeToPtr(Node *node) { return reinterpret_cast<void *>(node); }
};

template <typename T> class BatchedLFQueue {
public:
  using Task = T;
  using TaskPtr = T *;
  using Node = IntrusiveNode<Task>;
  using NodePtr = Node *;
  using List = IntrusiveList<Task>;

  void Push(TaskPtr task) { stack_.Push(task); }

  void Push(List list) {
    List reversed = ReverseList(std::move(list));
    stack_.Push(std::move(reversed));
  }

  List TryPopAll() {
    auto *batch = stack_.TryPopAll();
    if (batch == nullptr) {
      return {};
    }

    return CreateReverseList(batch);
  }

private:
  BatchedLFStack<T> stack_;

  List CreateReverseList(NodePtr node_base) {
    List list;

    auto *current_node = node_base;
    while (current_node != nullptr) {
      auto *next = current_node->next_;
      list.PushFront(current_node);
      current_node = next;
    }

    return list;
  }

  List ReverseList(List list) {
    List reversed;

    while (auto *node = list.PopFront()) {
      reversed.PushFront(node);
    }

    return reversed;
  }
};