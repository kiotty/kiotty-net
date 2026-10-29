cmake_minimum_required(VERSION 3.20)

project(kiotty_net LANGUAGES C CXX)

option(KIOTTY_NET_USE_TLS "Build the OpenSSL TLS backend (OFF builds the stub)" ON)
option(KIOTTY_NET_BUILD_TESTS "Build unit and integration tests" OFF)
option(KIOTTY_NET_BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
set(KIOTTY_NET_SANITIZER "" CACHE STRING "Sanitizer for every target: address, thread, undefined or a comma list")

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(KIOTTY_NET_SANITIZER)
    add_compile_options(-fsanitize=${KIOTTY_NET_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${KIOTTY_NET_SANITIZER})
endif()

add_library(kiotty_net_warnings INTERFACE)
target_compile_options(kiotty_net_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

set(KIOTTY_NET_SOURCES
    src/kiotty_net_statemachine.cpp
    src/implementation/core/kiotty_clock.cpp
    src/implementation/core/kiotty_pool.cpp
    src/implementation/core/kiotty_stage.cpp
    src/implementation/base/error/kiotty_socket_error.cpp
    src/implementation/base/socket/kiotty_socket.cpp
)

if(WIN32)
    list(APPEND KIOTTY_NET_SOURCES
        src/implementation/base/error/backend/kiotty_windows_error.cpp
        src/implementation/base/socket/backend/kiotty_windows_socket.cpp
    )
else()
    list(APPEND KIOTTY_NET_SOURCES
        src/implementation/base/error/backend/kiotty_posix_error.cpp
        src/implementation/base/socket/backend/kiotty_posix_socket.cpp
    )
endif()

add_library(kiotty_net STATIC ${KIOTTY_NET_SOURCES})

target_include_directories(kiotty_net
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/implementation
)

if(MSVC)
    target_compile_features(kiotty_net PUBLIC cxx_std_14)
else()
    target_compile_features(kiotty_net PUBLIC cxx_std_11)
endif()
target_compile_features(kiotty_net PUBLIC c_std_99)

target_link_libraries(kiotty_net PRIVATE kiotty_net_warnings)

if(KIOTTY_NET_USE_TLS)
    find_package(OpenSSL 3 REQUIRED)
    target_link_libraries(kiotty_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(kiotty_net PRIVATE KIOTTY_NET_USE_TLS=1)
else()
    target_compile_definitions(kiotty_net PRIVATE KIOTTY_NET_USE_TLS=0)
endif()

if(WIN32)
    target_link_libraries(kiotty_net PUBLIC ws2_32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(kiotty_net PUBLIC Threads::Threads)

if(KIOTTY_NET_BUILD_TESTS OR KIOTTY_NET_BUILD_BENCHMARKS)
    target_compile_definitions(kiotty_net PRIVATE KIOTTY_NET_TESTING=1)
endif()

if(KIOTTY_NET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
elseif(KIOTTY_NET_BUILD_BENCHMARKS)
    add_subdirectory(tests/benchmark)
endif()
