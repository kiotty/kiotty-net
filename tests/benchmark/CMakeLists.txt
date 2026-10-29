include(FetchContent)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(benchmark)

add_executable(kiotty_net_benchmarks
    kiotty_bench_core.cpp
)
target_include_directories(kiotty_net_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/implementation
)
target_compile_features(kiotty_net_benchmarks PRIVATE cxx_std_14)
target_link_libraries(kiotty_net_benchmarks PRIVATE kiotty_net benchmark::benchmark_main)
