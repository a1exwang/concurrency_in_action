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
    auto new_node = NodePtr(new Node<T>());
    new_node->data_ = std::make_unique<T>(std::move(data));

    {
      std::lock_guard _(big_lock_);
      assert(tail_->next_.get() == nullptr);
      tail_->next_ = std::move(new_node);
      tail_ = tail_->next_.get();
    }
    cv_all_done_.notify_one();
  }

  [[nodiscard]]
  bool empty() const {
    std::unique_lock _(big_lock_);
    return empty_unsafe() || all_done_;
  }

  std::optional<T> try_pop_front() {
    std::unique_lock _(big_lock_);
    if (empty_unsafe()) {
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

  std::optional<T> pop_front() {
    std::unique_lock lock(big_lock_);
    while (empty_unsafe() && !all_done_) {
      cv_all_done_.wait(lock);
    }
    if (all_done_) {
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

  void all_done() {
    all_done_ = true;
    cv_all_done_.notify_all();
  }

 private:
  bool empty_unsafe() const {
    return head_.get() == tail_;
  }

 private:
  NodePtr<T> head_;
  Node<T> *tail_;
  mutable std::mutex big_lock_;

  std::atomic<bool> all_done_ = false;
  mutable std::condition_variable cv_all_done_;
};
