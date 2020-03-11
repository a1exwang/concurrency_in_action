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

    cv_.notify_one();
  }

  [[nodiscard]]
  bool empty() const {
    std::lock_guard<std::mutex> lock_from(head_lock_);
    std::lock_guard<std::mutex> lock_to(tail_lock_);
    return empty_unsafe() || all_done_;
  }

  std::optional<T> try_pop_front() {
    NodePtr<T> real_head;
    {
      std::lock_guard _(head_lock_);
      if (empty_unsafe()) {
        return {};
      }
      assert(head_);
      assert(head_->next_);
      assert(!head_->data_);

      real_head = std::move(head_->next_);
      {
        std::lock_guard _2(tail_lock_);
        // only one element
        if (real_head.get() == tail_) {
          tail_ = head_.get();
        }
        head_->next_ = std::move(real_head->next_);
      }
    }
    return std::optional<T>(std::move(*real_head->data_));
  }

  std::optional<T> pop_front() {
    NodePtr<T> real_head;
    {
      std::unique_lock lock(head_lock_);
      while (empty_unsafe() && !all_done_) {
        cv_.wait(lock);
      }
      if (all_done_) {
        return {};
      }
      assert(head_);
      assert(head_->next_);
      assert(!head_->data_);

      real_head = std::move(head_->next_);
      {
        std::lock_guard _(tail_lock_);
        if (real_head.get() == tail_) {
          tail_ = head_.get();
        }
        head_->next_ = std::move(real_head->next_);
      }
    }
    return std::optional<T>(std::move(*real_head->data_));
  }

  void all_done() {
    all_done_ = true;
    cv_.notify_all();
  }

 private:
  bool empty_unsafe() const {
    return head_.get() == tail_;
  }

 private:
  NodePtr<T> head_;
  Node<T> *tail_;
  /* Always lock in the order of: head, tail, to prevent deadlock */
  mutable std::mutex head_lock_;
  mutable std::mutex tail_lock_;
//  mutable std::mutex all_done_lock_;
  std::atomic<bool> all_done_;
  mutable std::condition_variable cv_;
};

