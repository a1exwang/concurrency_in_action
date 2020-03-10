#include <cassert>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>

#include "concurrent_queue.hpp"

template <typename T>
class Node {
 public:
  std::unique_ptr<Node<T>> next_;
  std::unique_ptr<T> data_;
};

template <typename T>
requires IsConcurrentQueueElementType<T>
class MutexQueue {
 public:
  typedef T ElementType;
  MutexQueue() : head_(std::unique_ptr<Node<T>, void(&)(Node<T>*)>(new Node<T>(), queue_node_deleter)),
                 tail_(head_.get()) {}
  void push_back(T data) {
    std::unique_lock _(big_lock_);
    auto new_node = std::make_unique<Node<T>>();
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
  static void queue_node_deleter(Node<T> *ptr) {
    auto current = ptr;
    while (current) {
      auto next = current->next_.release();
      delete current;
      current = next;
    }
  }

 private:
  std::unique_ptr<Node<T>, void(&)(Node<T>*)> head_;
  Node<T> *tail_;
  mutable std::mutex big_lock_;
};

struct Big {
  explicit Big(int id) : data_(1024, 0), id_(id) {}
  [[nodiscard]] int id() const {
    return id_;
  }
  std::vector<char> data_;
  int id_;
};


struct ConsumerActionBusyWait {
  template <typename QueueType>
  requires BusyConcurrentQueue<QueueType>
  size_t operator()(
      QueueType &queue, size_t total_work, std::atomic<size_t> &global_work_done, bool verbose) const {

    size_t local_work_count = 0;
    while (true) {
      auto data = queue.try_pop_front();
      if (!data.has_value()) {
        if (global_work_done.load(std::memory_order_relaxed) >= total_work) {
          break;
        } else {
          continue;
        }
      }
      auto work_id_done = data.value().id();
      auto total_work_done =
          global_work_done.fetch_add(1, std::memory_order_relaxed) + 1;

      // process_work(data.value())
      local_work_count++;
      if (verbose) {
        std::cout << "work " << work_id_done << " done" << std::endl;
      }

      if (total_work_done > total_work) {
        break;
      }
      if (total_work_done == total_work) {
        break;
      }
    }

    return local_work_count;
  }
};


int main(int argc, char **argv) {
  if (argc < 5) {
    std::cerr << "invalid argument" << std::endl;
    return EXIT_FAILURE;
  }
  auto producer_count = std::stoul(argv[1]);
  auto consumer_count = std::stoul(argv[2]);
  auto total_work = std::stoul(argv[3]);
  auto verbose = std::stoul(argv[4]);
  profile_queue<MutexQueue<Big>, ConsumerActionBusyWait>(
      producer_count, consumer_count, total_work, verbose);
}
