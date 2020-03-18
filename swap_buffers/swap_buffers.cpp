#include <cstdlib>

#include "lock_based/lock_based_swap_buffer.hpp"

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "invalid argument" << std::endl;
    return EXIT_FAILURE;
  }
  size_t work_count = strtoul(argv[1], nullptr, 10);
  test_swap_buffer(work_count);
}