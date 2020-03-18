#pragma once


#include <cassert>
#include <cstddef>

#include <atomic>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>
#include <condition_variable>

// single consumer, single producer
class LockBasedSwapBuffer {
 public:
  LockBasedSwapBuffer(size_t buffer_size, size_t buffer_count = 2)
      :buffers_(buffer_count, std::vector<char>(buffer_size, 0)) {
    assert(buffer_count >= 2);
    assert(buffer_size > 0);
  }
  void write(const void *data, size_t size) {
    {
      std::unique_lock _(big_lock_);
      if (is_reading_) {
        writing_buffer_ = (reading_buffer_ + 1) % buffers_.size();
      } else {
        writing_buffer_ = reading_buffer_;
      }
      assert(!is_writing_);
      is_writing_ = true;
    }

    std::copy((const char*)data, (const char*)data + size, buffers_[writing_buffer_].data());

    {
      std::unique_lock _(big_lock_);
      assert(is_writing_);
      is_writing_ = false;
      write_counter++;
    }
  }
  bool read(void *data, size_t size) {
    {
      std::unique_lock _(big_lock_);

      if (write_counter == read_counter) {
        return false;
      }
      read_counter++;

      if (is_writing_) {
        reading_buffer_ = (writing_buffer_ + 1) % buffers_.size();
      } else {
        reading_buffer_ = writing_buffer_;
      }
      assert(!is_reading_);
      is_reading_ = true;
    }

    std::copy(buffers_[reading_buffer_].data(), buffers_[reading_buffer_].data()+size, (char*)data);

    {
      std::unique_lock _(big_lock_);
      assert(is_reading_);
      is_reading_ = false;
    }
    return true;
  }
 private:
  bool is_reading_ = false;
  bool is_writing_ = false;
  size_t reading_buffer_ = 0;
  size_t writing_buffer_ = 0;
  size_t read_counter = 0;
  size_t write_counter = 0;
  std::vector<std::vector<char>> buffers_;
  std::mutex big_lock_;
};

struct DataItem {
  size_t id;
  size_t produced_ns;
};

static size_t now_ns() {
  return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

void test_swap_buffer(size_t work_count) {
  size_t buffer_size = 16ul * 1024ul;
  size_t buffer_count = 2;
  size_t producer_count = 1;
  size_t consumer_count = 1;
  std::chrono::microseconds producer_interval(500);

  LockBasedSwapBuffer buffer(buffer_size, buffer_count);
  std::condition_variable cv_start_;
  std::mutex cv_start_lock_;
  bool consumer_start = false;
  bool producer_start = false;

  std::vector<std::thread> producers;
  std::atomic<bool> done{false};
  for (size_t producer_id = 0; producer_id < producer_count; producer_id++) {
    producers.emplace_back([&]() {
      auto data = std::make_unique<char[]>(buffer_size);

      {
        std::unique_lock lock(cv_start_lock_);
        while (!producer_start) {
          cv_start_.wait(lock);
        }
      }

      for (size_t i = 0; i < work_count; i++) {
        auto &item = *reinterpret_cast<DataItem*>(data.get());
        item.id = i;
        std::this_thread::sleep_for(producer_interval);
        item.produced_ns = now_ns();
        buffer.write(data.get(), buffer_size);
      }

      done = true;
    });
  }

  std::vector<std::thread> consumers;
  std::vector<double> data_average_latencies(consumer_count, 0);
  std::vector<uint8_t> data_read_count(work_count, 0);
  for (size_t consumer_id = 0; consumer_id < consumer_count; consumer_id++) {
    consumers.emplace_back([consumer_id, buffer_size,
                            &cv_start_, &cv_start_lock_, &consumer_start, &done, &buffer,
                            &data_read_count, &data_average_latencies ]() {
      auto data = std::make_unique<char[]>(buffer_size);
      size_t total_latency = 0;
      size_t items_read = 0;

      {
        std::unique_lock lock(cv_start_lock_);
        while (!consumer_start) {
          cv_start_.wait(lock);
        }
      }

      while (!done) {
        if (buffer.read(data.get(), buffer_size)) {
          auto &item = *reinterpret_cast<DataItem*>(data.get());
          auto now = now_ns();
          auto data_latency = now - item.produced_ns;
          total_latency += data_latency;
          items_read++;
          data_read_count[item.id]++;
        }
      }

      if (items_read > 0) {
        data_average_latencies[consumer_id] = (double)total_latency / items_read;
      }
    });
  }

  {
    std::unique_lock lock(cv_start_lock_);
    consumer_start = true;
    cv_start_.notify_all();
  }
  {
    std::unique_lock lock(cv_start_lock_);
    producer_start = true;
    cv_start_.notify_all();
  }

  for (auto &t : producers) {
    t.join();
  }
  for (auto &t : consumers) {
    t.join();
  }

  size_t ok_count = 0;
  size_t exceeding_count = 0;
  for (size_t i = 0; i < data_read_count.size(); i++) {
    if (data_read_count[i]) {
      if (data_read_count[i] > 1) {
        exceeding_count++;
      }
      ok_count++;
    }
  }
  // FIXME: exceeding count sometimes >= 1, is this a bug?
  std::cout << "ok count " << ok_count << "/" << exceeding_count << "/" << data_read_count.size() << std::endl;
  std::cout << "data latencies(ns): ";
  for (size_t i = 0; i < data_average_latencies.size(); i++)  {
    std::cout << data_average_latencies[i] << " ";
  }
  std::cout << std::endl;
}