add_executable(concurrent_queue main.cpp concurrent_queue.cpp)
target_link_libraries(concurrent_queue PRIVATE pthread)
