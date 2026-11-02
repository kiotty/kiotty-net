cmake_minimum_required(VERSION 3.20)

project(kiotty_net LANGUAGES C CXX)

option(KIOTTY_NET_USE_TLS "Use OpenSSL (fetched by openssl-cmake) for TLS. OFF builds the stub backend without OpenSSL" ON)
option(KIOTTY_NET_OPENSSL_PREBUILT "Download prebuilt static OpenSSL from openssl-cmake. OFF builds OpenSSL from source" ON)
option(KIOTTY_NET_TLS_STATIC_POOL "Route OpenSSL allocations into the static TLS pool. Only for OpenSSL linked statically into kiotty_net" ON)
set(KIOTTY_NET_MAX_OWNERS "16" CACHE STRING "Servers and clients one context can hold at once, 1 to 16")
set(KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER "128" CACHE STRING "Upper bound of max_clients, 1 to 1024")
set(KIOTTY_NET_MAX_RECV_CAPACITY "16384" CACHE STRING "Upper bound of recv_capacity in bytes")
set(KIOTTY_NET_MAX_SEND_CAPACITY "16384" CACHE STRING "Upper bound of send_capacity in bytes")
set(KIOTTY_NET_MAX_PUSH_CAPACITY "16384" CACHE STRING "Upper bound of push_capacity in bytes")
set(KIOTTY_NET_MAX_TLS_CONTEXTS "1" CACHE STRING "Contexts that use TLS at once. Sizes the static TLS pool")
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
    src/implementation/core/kiotty_stage.cpp
    src/implementation/core/kiotty_pool.cpp
    src/implementation/base/context/kiotty_context.cpp
    src/implementation/base/error/kiotty_socket_error.cpp
    src/implementation/base/error/backend/kiotty_posix_error.cpp
    src/implementation/base/error/backend/kiotty_windows_error.cpp
    src/implementation/base/socket/kiotty_socket.cpp
    src/implementation/base/socket/backend/kiotty_posix_socket.cpp
    src/implementation/base/socket/backend/kiotty_windows_socket.cpp
    src/implementation/base/event/kiotty_io_action.cpp
    src/implementation/base/event/backend/kiotty_epoll_event_listener.cpp
    src/implementation/base/event/backend/kiotty_iocp_event_listener.cpp
    src/implementation/base/tls/kiotty_tls_engine.cpp
    src/implementation/base/tls/backend/kiotty_openssl_tls.cpp
    src/implementation/base/tls/backend/kiotty_stub_tls.cpp
)

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

target_compile_definitions(kiotty_net PUBLIC
    KIOTTY_NET_MAX_OWNERS=${KIOTTY_NET_MAX_OWNERS}
    KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER=${KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER}
    KIOTTY_NET_MAX_RECV_CAPACITY=${KIOTTY_NET_MAX_RECV_CAPACITY}
    KIOTTY_NET_MAX_SEND_CAPACITY=${KIOTTY_NET_MAX_SEND_CAPACITY}
    KIOTTY_NET_MAX_PUSH_CAPACITY=${KIOTTY_NET_MAX_PUSH_CAPACITY}
    KIOTTY_NET_MAX_TLS_CONTEXTS=${KIOTTY_NET_MAX_TLS_CONTEXTS}
)

if(KIOTTY_NET_USE_TLS)
    include(FetchContent)
    FetchContent_Declare(
        openssl-cmake
        GIT_REPOSITORY https://github.com/bn3monkey/openssl-cmake.git
        GIT_TAG        v1.1.0
    )
    set(OPENSSL_CMAKE_USE_PREBUILT ${KIOTTY_NET_OPENSSL_PREBUILT} CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(openssl-cmake)
    target_link_libraries(kiotty_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_USE_TLS=1)
    if(KIOTTY_NET_TLS_STATIC_POOL)
        target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_TLS_STATIC_POOL=1)
    else()
        target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_TLS_STATIC_POOL=0)
    endif()
else()
    target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_USE_TLS=0 KIOTTY_NET_TLS_STATIC_POOL=0)
endif()

if(WIN32)
    target_link_libraries(kiotty_net PUBLIC ws2_32 ntdll)
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
