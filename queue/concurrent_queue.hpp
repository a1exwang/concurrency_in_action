#pragma once

#include <cstddef>

#include <atomic>
#include <condition_variable>
#include <functional>
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
  { queue.all_done() };
  { queue.pop_front() } -> std::optional<typename QueueType::ElementType>;
};


template <typename ConsumerActionType, typename QueueType>
concept IsConsumerAction = requires(
    ConsumerActionType consumer_action,
    QueueType queue,
    std::function<void(typename QueueType::ElementType &)> process_work,
    size_t consumer_id,
    size_t total_work,
    std::atomic<size_t> &global_work_done, bool verbose) {
  { ConsumerActionType() };
  { consumer_action(queue, process_work, consumer_id, total_work, global_work_done, verbose) };
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

using my_clock = std::chrono::steady_clock;

template <typename QueueType, typename ConsumerAction,
          typename ElementType = typename QueueType::ElementType>
requires BasicConcurrentQueue<QueueType> &&
    IsConcurrentQueueElementType<ElementType> &&
    IsConsumerAction<ConsumerAction, QueueType>
void profile_queue(
    const std::string &name,
    size_t producer_count,
    size_t consumer_count,
    size_t total_work,
    bool verbose) {

  QueueType queue;
  std::mutex start_cv_lock;
  std::condition_variable start_cv;
  bool producer_start = false;
  bool consumer_start = false;
  ConsumerAction consumer_action;

  std::atomic<int64_t> remaining_work{static_cast<int64_t>(total_work)};
  std::vector<std::thread> threads;
  my_clock::time_point producer_start_time,
      consumer_finish_time;
  std::vector<size_t> producer_local_work_count(producer_count),
      consumer_local_work_count(consumer_count);
  for (size_t i = 0; i < producer_count; i++) {
    threads.emplace_back([&queue, &start_cv_lock, &start_cv, i, &producer_start,
                             &remaining_work, &producer_start_time,
                             &producer_local_work_count, verbose, producer_id = i]() {
      {
        std::unique_lock ul(start_cv_lock);
        while (!producer_start) {
          start_cv.wait(ul);
        }
      }
      size_t local_work_count = 0;
      if (verbose) {
        std::cout << "producer " << i << " started" << std::endl;
      }

      while (true) {
        auto work_id =
            remaining_work.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (work_id >= 0) {
          if (verbose) {
            std::cout << "producer " << producer_id << " got work " << work_id << std::endl;
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
  std::vector<my_clock::time_point> local_finish_time(consumer_count);

  std::vector<std::atomic<uint8_t>> work_mask(total_work);
  auto process_work = [&work_mask](ElementType &element) {
    work_mask[element.id()]++;
  };

  for (size_t i = 0; i < consumer_count; i++) {
    threads.emplace_back([&counter, &queue, &start_cv_lock, &start_cv, i, &consumer_start,
                             total_work, &consumer_finish_time,
                             &consumer_local_work_count, verbose, consumer_action,
                             &local_finish_time,
                             &process_work]() {
      {
        std::unique_lock ul(start_cv_lock);
        while (!consumer_start) {
          start_cv.wait(ul);
        }
      }

      if (verbose) {
        std::cout << "consumer " << i << " started" << std::endl;
      }
      consumer_local_work_count[i] = consumer_action(queue, process_work, i, total_work, counter, verbose);
      if (verbose) {
        std::cout << "consumer " << i << " ended" << std::endl;
      }

      local_finish_time[i] = my_clock::now();
    });
  }

  if (verbose) {
    std::cout << "notify all" << std::endl;
  }

  producer_start_time = my_clock::now();
  {
    std::unique_lock _(start_cv_lock);
    consumer_start = true;
  }
  start_cv.notify_all();

  {
    std::unique_lock _(start_cv_lock);
    producer_start = true;
  }
  start_cv.notify_all();

  for (auto &t : threads) {
    t.join();
  }
  consumer_finish_time = *std::max_element(local_finish_time.begin(), local_finish_time.end());

  bool passed = true;
  for (size_t i = 0; i < work_mask.size(); i++) {
    if (work_mask[i] != 1) {
      std::cout << "Error: work " << i << " has been done " << (int)work_mask[i] << " times" << std::endl;
      passed = false;
    }
  }
  if (!passed) {
    std::cout << "test case '" << name << "' failed" << std::endl;
    return;
  }

  std::cout << "test case '" << name << "' passed" << std::endl;
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
  auto total_time = std::chrono::duration<double>(consumer_finish_time-producer_start_time).count();
  std::cout << "total time: "
            << total_time
            << "s" << std::endl;
  std::cout << "total throughput " << pretty_number(counter.load() / total_time) << "/s" << std::endl;
  std::cout << std::endl;
}
