#pragma once

#include <cassert>

#include <memory>

#include "concurrent_queue.hpp"
#include "node.hpp"


template <typename T>
requires IsConcurrentQueueElementType<T>
class MutexQueueWithBigLock {
 public:
  typedef T ElementType;
  MutexQueueWithBigLock() : head_(NodePtr(new Node<T>())),
                            tail_(head_.get()) {}
  void push_back(T data) {
    std::unique_lock _(big_lock_);
    auto new_node = NodePtr(new Node<T>());
    new_node->data_ = std::make_unique<T>(std::move(data));

    assert(tail_->next_.get() == nullptr);
    tail_->next_ = std::move(new_node);
    tail_ = tail_->next_.get();
  }

  [[nodiscard]]
  bool empty() const {
    std::unique_lock _(big_lock_);
    return head_.get() == tail_;
  }

  std::optional<T> try_pop_front() {
    std::unique_lock _(big_lock_);
    if (head_.get() == tail_) {
      return {};
    }
    assert(head_);
    assert(head_->next_);
    assert(!head_->data_);

    auto real_head = std::move(head_->next_);
    if (real_head.get() == tail_) {
      tail_ = head_.get();
    }
    head_->next_ = std::move(real_head->next_);
    return std::optional<T>(std::move(*real_head->data_));
  }

 private:
  NodePtr<T> head_;
  Node<T> *tail_;
  mutable std::mutex big_lock_;
};
