#pragma once

template <typename T>
requires IsConcurrentQueueElementType<T>
class MutexQueueWith2Locks {
 public:
  typedef T ElementType;
  MutexQueueWith2Locks() : head_(NodePtr(new Node<T>())),
                           tail_(head_.get()) {}
  void push_back(T data) {
    auto new_node = NodePtr(new Node<T>());
    new_node->data_ = std::make_unique<T>(std::move(data));

    {
      std::lock_guard _(tail_lock_);
      assert(tail_->next_.get() == nullptr);
      tail_->next_ = std::move(new_node);
      tail_ = tail_->next_.get();
    }
  }

  [[nodiscard]]
  bool empty() const {
    std::lock_guard<std::mutex> lock_from(head_lock_);
    std::lock_guard<std::mutex> lock_to(tail_lock_);
    return head_.get() == tail_;
  }

  std::optional<T> try_pop_front() {
    NodePtr<T> real_head;
    {
      std::lock_guard _(head_lock_);
      if (head_.get() == tail_) {
        return {};
      }
      assert(head_);
      assert(head_->next_);
      assert(!head_->data_);

      real_head = std::move(head_->next_);
      {
        std::lock_guard _2(tail_lock_);
        if (real_head.get() == tail_) {
          tail_ = head_.get();
        }
        head_->next_ = std::move(real_head->next_);
      }
    }
    return std::optional<T>(std::move(*real_head->data_));
  }

 private:
  NodePtr<T> head_;
  Node<T> *tail_;
  /* Always lock in the order of: head, tail, to prevent deadlock */
  mutable std::mutex head_lock_;
  mutable std::mutex tail_lock_;
};

