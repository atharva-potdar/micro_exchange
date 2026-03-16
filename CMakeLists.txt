cmake_minimum_required(VERSION 4.0)
project(micro_exchange LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Common warnings (interface library) ---
add_library(project_warnings INTERFACE)
target_compile_features(project_warnings INTERFACE cxx_std_20)
target_compile_options(project_warnings INTERFACE
    -Wall -Wextra -Werror -Wpedantic -Wconversion
)

# --- Per-config & SIMD flags ---
add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE
    $<$<CONFIG:Debug>:-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O3 -march=native -flto=thin>
)
target_link_options(project_options INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Release>:-flto=thin>
)

# ==============================================================================
# --- CORE LIBRARY ---
# This holds all your business logic (matmul, orderbook, etc.)
# By making it a STATIC library, main, tests, and benches can all share it.
# ==============================================================================
add_library(micro_exchange_core INTERFACE 
  # src/orderbook/orderbook.cpp, and so on
)
target_include_directories(micro_exchange_core INTERFACE ${CMAKE_SOURCE_DIR}/src)

# --- Main executable ---
add_executable(micro_exchange src/main.cpp)
# Link the main executable to our core library
target_link_libraries(micro_exchange PRIVATE micro_exchange_core)

# --- Tests ---
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS test/test_*.cpp)
if(TEST_SOURCES)
    find_package(GTest REQUIRED)
    foreach(src IN LISTS TEST_SOURCES)
        cmake_path(GET src STEM name)
        add_executable(${name} ${src})
        # Link the test to our core library
        target_link_libraries(${name} PRIVATE micro_exchange_core GTest::gtest_main)
        gtest_discover_tests(${name})
    endforeach()
endif()

# --- Benchmarks ---
file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS bench/bench_*.cpp)
if(BENCH_SOURCES)
    find_package(benchmark REQUIRED)
    foreach(src IN LISTS BENCH_SOURCES)
        cmake_path(GET src STEM name)
        add_executable(${name} ${src})
        # Link the benchmark to our core library
        target_link_libraries(${name} PRIVATE micro_exchange_core benchmark::benchmark_main)
    endforeach()
endif()
