#pragma once

#include "util/noncopyable.hpp"

#include <cstddef>

template <typename T> struct IntrusiveNode {
  IntrusiveNode *next_ = nullptr;

  T *Item() { return static_cast<T *>(this); }
};

template <typename T> class IntrusiveList : private NonCopyable {
public:
  using Node = IntrusiveNode<T>;

  IntrusiveList() = default;

  IntrusiveList(IntrusiveList &&other) {
    head_ = other.head_;
    tail_ = other.tail_;
    size_ = other.size_;
    other.Clear();
  }

  IntrusiveList &operator=(IntrusiveList &&other) {
    head_ = other.head_;
    tail_ = other.tail_;
    size_ = other.size_;
    other.Clear();
    return *this;
  }

  void Insert(IntrusiveList<T> &&other) {
    if (other.Empty()) {
      return;
    }
    if (Empty()) {
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
    } else {
      tail_->next_ = other.head_;
      tail_ = other.tail_;
      size_ += other.size_;
    }
    other.Clear();
  }

  void PushBack(Node *node) {
    ++size_;

    node->next_ = nullptr;
    if (Empty()) {
      head_ = node;
      tail_ = node;
    } else {
      tail_->next_ = node;
      tail_ = node;
    }
  }

  void PushFront(Node *node) {
    ++size_;

    if (Empty()) {
      node->next_ = nullptr;
      head_ = node;
      tail_ = node;
    } else {
      node->next_ = head_;
      head_ = node;
    }
  }

  T *PopFront() {
    if (Empty()) {
      return nullptr;
    }

    --size_;

    auto *current_head = head_;
    if (head_ != tail_) {
      head_ = head_->next_;
    } else {
      head_ = nullptr;
      tail_ = nullptr;
    }
    current_head->next_ = nullptr;

    return current_head->Item();
  }

  void Clear() {
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
  }

  bool Empty() const { return head_ == nullptr; }

  size_t Size() const { return size_; }

  Node *Head() { return head_; }

  Node *Tail() { return tail_; }

private:
  Node *head_{nullptr};
  Node *tail_{nullptr};
  size_t size_{0};
};
