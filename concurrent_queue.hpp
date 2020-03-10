#pragma once

#include <cstddef>

#include <atomic>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

template <typename ElementType>
concept IsConcurrentQueueElementType = requires(ElementType element, size_t id) {
  { ElementType(id) };
  { element.id() } -> size_t;
};

template <typename QueueType, typename ElementType>
concept CanPushBackElement = requires(QueueType queue, ElementType element) {
  { queue.push_back(element) } -> void;
};

template<typename QueueType>
concept BasicConcurrentQueue = requires(QueueType queue, const QueueType &queue_const) {
  typename QueueType::ElementType;
  requires CanPushBackElement<QueueType, typename QueueType::ElementType>;
  { queue_const.empty() } -> bool;
};

template<typename QueueType>
concept BusyConcurrentQueue = requires(QueueType queue, const QueueType &queue_const) {
  requires BasicConcurrentQueue<QueueType>;
  { queue.try_pop_front() } -> std::optional<typename QueueType::ElementType>;
};

template<typename QueueType>
concept IdleConcurrentQueue = requires(QueueType queue, const QueueType &queue_const) {
  requires BasicConcurrentQueue<QueueType>;
  { queue.pop_front() } -> std::optional<typename QueueType::ElementType>;
};


template <typename ConsumerActionType, typename QueueType>
concept IsConsumerAction = requires(
    ConsumerActionType consumer_action, QueueType queue,
    size_t total_work, std::atomic<size_t> &global_work_done, bool verbose) {
  { ConsumerActionType() };
  { consumer_action(queue, total_work, global_work_done, verbose) };
};

static std::string pretty_number(size_t n) {
  static const char *suffixes[] = {"", "K", "M", "G", "T"};
  static size_t radix = 1000;
  size_t remainder = 0;
  size_t last_remainder = 0;
  size_t i = 0;
  while (true) {
    remainder = n % radix;
    n /= radix;

    if (n == 0 || i == sizeof(suffixes)/sizeof(suffixes[0])) {
      if (i == 0) {
        return std::to_string(remainder) + suffixes[i];
      } else {
        std::stringstream ss;
        ss << remainder << "." << std::setw(3) << std::setfill('0') << last_remainder << suffixes[i];
        return ss.str();
      }
    }

    last_remainder = remainder;
    i++;
  }
}

template <typename QueueType, typename ConsumerAction,
          typename ElementType = typename QueueType::ElementType>
requires BasicConcurrentQueue<QueueType> &&
    IsConcurrentQueueElementType<ElementType> &&
    IsConsumerAction<ConsumerAction, QueueType>
void profile_queue(
    size_t producer_count, size_t consumer_count, size_t total_work,
    bool verbose) {
  QueueType queue;
  std::mutex lock;
  std::condition_variable cv;
  bool producer_start = false;
  bool consumer_start = false;
  ConsumerAction consumer_action;

  std::atomic<int64_t> remaining_work{static_cast<int64_t>(total_work)};
  std::vector<std::thread> threads;
  std::chrono::high_resolution_clock::time_point producer_start_time,
      consumer_finish_time;
  std::vector<size_t> producer_local_work_count(producer_count),
      consumer_local_work_count(consumer_count);
  for (size_t i = 0; i < producer_count; i++) {
    threads.emplace_back([&queue, &lock, &cv, i, &producer_start,
                             &remaining_work, &producer_start_time,
                             &producer_local_work_count, verbose]() {
      {
        std::unique_lock ul(lock);
        while (!producer_start) {
          cv.wait(ul);
        }
      }
      size_t local_work_count = 0;
      if (verbose) {
        std::cout << "producer " << i << " started" << std::endl;
      }
      producer_start_time = std::chrono::high_resolution_clock::now();

      while (true) {
        auto work_id =
            remaining_work.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (work_id >= 0) {
          if (verbose) {
            std::cout << "got work " << work_id << std::endl;
          }
          queue.push_back(ElementType(work_id));
          local_work_count++;
        } else {
          break;
        }
      }
      if (verbose) {
        std::cout << "producer " << i << " ended" << std::endl;
      }
      producer_local_work_count[i] = local_work_count;
    });
  }

  std::atomic<size_t> counter{0};
  for (size_t i = 0; i < consumer_count; i++) {
    threads.emplace_back([&counter, &queue, &lock, &cv, i, &consumer_start,
                             total_work, &consumer_finish_time,
                             &consumer_local_work_count, verbose, consumer_action]() {
      size_t local_work_count = 0;
      {
        std::unique_lock ul(lock);
        while (!consumer_start) {
          cv.wait(ul);
        }
      }
      if (verbose) {
        std::cout << "consumer " << i << " started" << std::endl;
      }
      consumer_action(queue, total_work, counter, verbose);
      if (verbose) {
        std::cout << "consumer " << i << " ended" << std::endl;
      }
      consumer_finish_time = std::chrono::high_resolution_clock::now();
      consumer_local_work_count[i] = local_work_count;
    });
  }

  if (verbose) {
    std::cout << "notify all" << std::endl;
  }
  {
    std::unique_lock _(lock);
    producer_start = true;
  }
  cv.notify_all();
  {
    std::unique_lock _(lock);
    consumer_start = true;
  }
  cv.notify_all();
  for (auto &t : threads) {
    t.join();
  }

  std::cout << "total work: " << total_work << std::endl;
  std::cout << "total producers: " << producer_count << ", each: ";
  for (size_t i = 0; i < producer_count; i++) {
    std::cout << producer_local_work_count[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "total consumers: " << consumer_count << ", each: ";
  for (size_t i = 0; i < consumer_count; i++) {
    std::cout << consumer_local_work_count[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "finished work: " << counter.load() << std::endl;
  auto total_time = std::chrono::duration<double>(consumer_finish_time - producer_start_time).count();
  std::cout << "total time: "
            << total_time
            << "s" << std::endl;
  std::cout << "total throughput " << pretty_number(counter.load() / total_time) << "/s" << std::endl;
}
