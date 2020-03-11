#pragma once

struct ConsumerActionIdleWait {
  template <typename QueueType>
  requires IdleConcurrentQueue<QueueType>
  size_t operator()(
      QueueType &queue,
      std::function<void(typename QueueType::ElementType &)> process_work,
      size_t consumer_id,
      size_t total_work,
      std::atomic<size_t> &global_work_done,
      bool verbose) const {

    size_t local_work_count = 0;
    my_clock::time_point finish_time;
    while (true) {
      auto data = queue.pop_front();
      if (!data.has_value()) {
        break;
      }

      auto work_id_done = data.value().id();
      auto total_work_done =
          global_work_done.fetch_add(1, std::memory_order_relaxed) + 1;

      process_work(data.value());
      local_work_count++;
      if (verbose) {
        std::cout << "consumer: " << consumer_id << " work " << work_id_done << " done" << std::endl;
      }

      if (total_work_done == total_work) {
        // This thread got the last element.
        queue.all_done();
        break;
      }
      // total_work_done should not be < total_work when using idle wait
      assert(total_work_done < total_work);
    }

    return local_work_count;
  }
};
