cmake_minimum_required(VERSION 3.8)
project(concurrent)

set(CMAKE_CXX_STANDARD 20)
if (CMAKE_CXX_COMPILER_ID STREQUAL GNU AND
        CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 9)
    add_compile_options(-fconcepts)
else()
    message(FATAL_ERROR "Only GCC >= 9.0 are supported because we use C++ 20 concepts")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_subdirectory(queue)
add_subdirectory(swap_buffers)

