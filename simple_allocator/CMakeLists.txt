cmake_minimum_required(VERSION 3.12)
project(CacheFriendlyAllocator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall") # Optimize for speed!

# 1. Find Threading support
find_package(Threads REQUIRED)

# 2. Build the Allocator Library
add_library(allocator_lib sources/allocator.cpp)
target_include_directories(allocator_lib PUBLIC headers)
target_link_libraries(allocator_lib PRIVATE Threads::Threads)

# 3. Enable Testing
enable_testing()

# 4. Unit Test Executable
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests allocator_lib)
add_test(NAME UnitTests COMMAND unit_tests)

# 5. Benchmark Executable
add_executable(benchmarks tests/benchmark_tests.cpp)
target_link_libraries(benchmarks allocator_lib Threads::Threads)
add_test(NAME BenchmarkTests COMMAND benchmarks)
