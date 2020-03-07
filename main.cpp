#include <cassert>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

template <typename T>
class Node {
 public:
  std::unique_ptr<Node<T>> next_;
  std::unique_ptr<T> data_;
};

template <typename T>
class Queue {
 public:
  Queue() : head_(std::make_unique<Node<T>>()), tail_(head_.get()) {}
  void push_back(T data) {
    std::unique_lock _(big_lock_);
    auto new_node = std::make_unique<Node<T>>();
    new_node->data_ = std::make_unique<T>(std::move(data));

    assert(tail_->next_.get() == nullptr);
    tail_->next_ = std::move(new_node);
    tail_ = tail_->next_.get();
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
  std::unique_ptr<Node<T>> head_;
  Node<T> *tail_;
  std::mutex big_lock_;
};

struct Big {
  Big(int id) : data_(1024, 0), id_(id) {}
  std::vector<char> data_;
  int id_;
};

int main() {
  Queue<Big> queue;
  size_t producer_count = 1;
  size_t consume_count = 1;
  size_t total_work = 16 * 1024;

  std::mutex lock;
  std::condition_variable cv;
  bool producer_start = false;
  bool consume_start = false;

  std::atomic<int64_t> remaining_work{static_cast<int64_t>(total_work)};
  std::vector<std::thread> threads;
  std::chrono::high_resolution_clock::time_point producer_start_time,
      consumer_finish_time;
  for (size_t i = 0; i < producer_count; i++) {
    threads.emplace_back([&queue, &lock, &cv, i, &producer_start,
                          &remaining_work, &producer_start_time]() {
      {
        std::unique_lock ul(lock);
        while (!producer_start) {
          cv.wait(ul);
        }
      }
      std::cout << "producer " << i << " started" << std::endl;
      producer_start_time = std::chrono::high_resolution_clock::now();

      while (true) {
        auto work_id =
            remaining_work.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (work_id >= 0) {
          // std::cout << "got work " << work_id << std::endl;
          queue.push_back(Big(work_id));
        } else {
          break;
        }
      }
      std::cout << "producer " << i << " ended" << std::endl;
    });
  }

  std::atomic<size_t> counter{0};
  for (size_t i = 0; i < consume_count; i++) {
    threads.emplace_back([&counter, &queue, &lock, &cv, i, &consume_start,
                          total_work, &consumer_finish_time]() {
      {
        std::unique_lock ul(lock);
        while (!consume_start) {
          cv.wait(ul);
        }
      }
      std::cout << "consumer " << i << " started" << std::endl;
      while (true) {
        auto data = queue.try_pop_front();
        if (data.has_value()) {
          // process_work(data.value())
          auto work_id_done = data.value().id_;
          auto total_work_done =
              counter.fetch_add(1, std::memory_order_relaxed) + 1;
          if (total_work_done > total_work) {
            break;
          }
          // std::cout << "work " << work_id_done << " done" << std::endl;
          if (total_work_done == total_work) {
            break;
          }
        }
      }
      std::cout << "consumer " << i << " ended" << std::endl;
      consumer_finish_time = std::chrono::high_resolution_clock::now();
    });
  }

  std::cout << "notify all" << std::endl;
  {
    std::unique_lock _(lock);
    producer_start = true;
  }
  cv.notify_all();
  {
    std::unique_lock _(lock);
    consume_start = true;
  }
  cv.notify_all();
  for (auto &t : threads) {
    t.join();
  }

  std::cout << "total work: " << total_work << std::endl;
  std::cout << "finished work: " << counter.load() << std::endl;
  std::cout << "total time: "
            << std::chrono::duration<double>(consumer_finish_time -
                                             producer_start_time)
                   .count()
            << "s" << std::endl;
}
