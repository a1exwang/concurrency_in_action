#include <iostream>
#include <optional>
#include <vector>
#include "mutex_queue_with_big_lock.hpp"
#include "mutex_queue_with_2_locks.hpp"
#include "consumer_action_busy_wait.hpp"

struct Big {
  explicit Big(int id) : data_(1024, 0), id_(id) {}
  [[nodiscard]] int id() const {
    return id_;
  }
  std::vector<char> data_;
  int id_;
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
  profile_queue<MutexQueueWithBigLock<Big>, ConsumerActionBusyWait>(
      "MutexQueueWithBigLock",
      producer_count, consumer_count, total_work, verbose);
  profile_queue<MutexQueueWith2Locks<Big>, ConsumerActionBusyWait>(
      "MutexQueueWith2Locks",
      producer_count, consumer_count, total_work, verbose);
}
