cmake_minimum_required(VERSION 3.20)

project(kiotty_net LANGUAGES C CXX)

option(KIOTTY_NET_USE_TLS "Use OpenSSL (fetched by openssl-cmake) for TLS. OFF builds the stub backend without OpenSSL" ON)
option(KIOTTY_NET_OPENSSL_PREBUILT "Download prebuilt static OpenSSL from openssl-cmake. OFF builds OpenSSL from source" ON)
option(KIOTTY_NET_BUILD_TESTS "Build unit and integration tests" OFF)
option(KIOTTY_NET_BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
option(KIOTTY_NET_WITH_IO_URING "Linux (not Android): use io_uring when the kernel allows it, epoll otherwise" ON)
option(KIOTTY_NET_WITH_LIBKQUEUE "Linux only: use the kqueue backend through libkqueue instead of epoll" OFF)
set(KIOTTY_NET_SANITIZER "" CACHE STRING "Sanitizer for every target: address, thread, undefined or a comma list")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wconversion;-Wsign-compare;-Wshadow;-Wswitch>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

add_library(kiotty_net_hardening INTERFACE)
target_compile_options(kiotty_net_hardening INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-rtti;-fno-exceptions>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fstack-protector-strong>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Release>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR-;/EHs-c->
    $<$<CXX_COMPILER_ID:MSVC>:/sdl>
)
target_compile_definitions(kiotty_net_hardening INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_HAS_EXCEPTIONS=0>
)

set(KIOTTY_NET_SOURCES
    src/kiotty_net_base.cpp
    src/kiotty_net_statemachine.cpp
    src/kiotty_net_http.cpp
    src/kiotty_net_http_client.cpp
    src/kiotty_net_websocket.cpp
    src/implementation/core/kiotty_clock.cpp
    src/implementation/core/kiotty_deadline.cpp
    src/implementation/core/kiotty_parse_integer.cpp
    src/implementation/core/kiotty_stage.cpp
    src/implementation/core/kiotty_text.cpp
    src/implementation/core/kiotty_sha1.cpp
    src/implementation/core/kiotty_base64.cpp
    src/implementation/base/domain/entity/kiotty_connection_event.cpp
    src/implementation/base/data/infra/error/kiotty_socket_error.cpp
    src/implementation/base/data/infra/error/backend/kiotty_posix_socket_error.cpp
    src/implementation/base/data/infra/error/backend/kiotty_windows_socket_error.cpp
    src/implementation/base/data/infra/socket/kiotty_connect_with_retry.cpp
    src/implementation/base/data/infra/socket/kiotty_stop_signal.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_socket_startup.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_socket_startup.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_address.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_active_socket.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_passive_socket.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_socket_transfer.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_address.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_active_socket.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_passive_socket.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_socket_transfer.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_posix_stop_signal.cpp
    src/implementation/base/data/infra/socket/backend/kiotty_windows_stop_signal.cpp
    src/implementation/base/domain/entity/kiotty_io_action.cpp
    src/implementation/base/data/infra/event/kiotty_event_listener.cpp
    src/implementation/base/data/infra/event/backend/kiotty_readiness_event_listener.cpp
    src/implementation/base/data/infra/event/backend/kiotty_readiness_event_ready.cpp
    src/implementation/base/data/infra/event/backend/kiotty_readiness_event_submit.cpp
    src/implementation/base/data/infra/event/backend/kiotty_readiness_event_wait.cpp
    src/implementation/base/data/infra/event/backend/kiotty_epoll_event_listener.cpp
    src/implementation/base/data/infra/event/backend/kiotty_epoll_event_wait.cpp
    src/implementation/base/data/infra/event/backend/kiotty_kqueue_event_listener.cpp
    src/implementation/base/data/infra/event/backend/kiotty_ring_event_open.cpp
    src/implementation/base/data/infra/event/backend/kiotty_ring_event_submit.cpp
    src/implementation/base/data/infra/event/backend/kiotty_ring_event_wait.cpp
    src/implementation/base/data/infra/event/backend/kiotty_kqueue_event_wait.cpp
    src/implementation/base/data/infra/event/backend/kiotty_iocp_event_listener.cpp
    src/implementation/base/data/infra/event/backend/kiotty_iocp_event_submit.cpp
    src/implementation/base/data/infra/event/backend/kiotty_iocp_event_wait.cpp
    src/implementation/base/data/infra/event/backend/kiotty_no_event_listener.cpp
    src/implementation/base/data/infra/tls/kiotty_tls_engine.cpp
    src/implementation/base/data/infra/tls/kiotty_tls_profile.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_openssl_context.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_openssl_session.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_openssl_tls_engine.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_openssl_tls_pipe.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_openssl_tls_profile.cpp
    src/implementation/base/data/infra/tls/backend/kiotty_stub_tls.cpp
    src/implementation/base/data/repository/stream/kiotty_connection_stream.cpp
    src/implementation/base/data/repository/stream/kiotty_connection_stream_line.cpp
    src/implementation/base/data/infra/file/backend/kiotty_posix_file.cpp
    src/implementation/base/data/infra/file/backend/kiotty_windows_file.cpp
    src/implementation/base/data/repository/connection/kiotty_acceptor.cpp
    src/implementation/base/data/repository/connection/kiotty_adopt_action.cpp
    src/implementation/base/data/repository/connection/kiotty_callback_handler.cpp
    src/implementation/base/data/repository/connection/kiotty_caller.cpp
    src/implementation/base/data/repository/connection/kiotty_receive_flow.cpp
    src/implementation/base/data/repository/connection/kiotty_send_flow.cpp
    src/implementation/base/data/repository/connection/kiotty_work_flow.cpp
    src/implementation/base/data/repository/connection/kiotty_connection.cpp
    src/implementation/base/data/repository/connection/kiotty_connection_parts.cpp
    src/implementation/base/data/repository/connection/kiotty_connection_host.cpp
    src/implementation/base/data/repository/connection/kiotty_notification_desk.cpp
    src/implementation/base/domain/usecases/io_loop/kiotty_io_loop.cpp
    src/implementation/base/domain/usecases/io_loop/kiotty_io_loop_step.cpp
    src/implementation/statemachine/data/repository/kiotty_rule_table.cpp
    src/implementation/statemachine/data/repository/kiotty_stateful_handler.cpp
    src/implementation/statemachine/domain/usecases/kiotty_statemachine_usecases.cpp
    src/implementation/http/data/repository/header/kiotty_http_token_rules.cpp
    src/implementation/http/data/repository/request/kiotty_http_request_line_parser.cpp
    src/implementation/http/data/repository/header/kiotty_http_header_line_parser.cpp
    src/implementation/http/data/repository/body/kiotty_http_message_framing_parser.cpp
    src/implementation/http/data/repository/server/kiotty_http_route_path_rules.cpp
    src/implementation/http/data/repository/response/kiotty_http_answer_rules.cpp
    src/implementation/http/data/repository/response/kiotty_http_send_rules.cpp
    src/implementation/http/data/repository/machine/kiotty_http_answer_writer.cpp
    src/implementation/http/data/repository/header/kiotty_http_content_type_rules.cpp
    src/implementation/http/data/repository/response/kiotty_http_response_text_builder.cpp
    src/implementation/http/data/repository/machine/kiotty_http_exchange.cpp
    src/implementation/http/data/repository/response/kiotty_http_status_line_parser.cpp
    src/implementation/http/data/repository/response/kiotty_http_reply_framing_rules.cpp
    src/implementation/http/data/repository/body/kiotty_http_body_plan_rules.cpp
    src/implementation/http/data/repository/body/kiotty_http_body_reader.cpp
    src/implementation/http/data/repository/request/kiotty_http_request_check_rules.cpp
    src/implementation/http/data/repository/request/kiotty_http_request_head_builder.cpp
    src/implementation/http/data/repository/body/kiotty_http_upload_rules.cpp
    src/implementation/http/data/repository/header/kiotty_http_reply_headers_parser.cpp
    src/implementation/websocket/data/repository/frame/kiotty_websocket_frame_parser.cpp
    src/implementation/websocket/data/repository/frame/kiotty_websocket_frame_builder.cpp
    src/implementation/websocket/data/repository/frame/kiotty_websocket_mask_rules.cpp
    src/implementation/websocket/data/repository/control/kiotty_websocket_close_rules.cpp
    src/implementation/websocket/data/repository/handshake/kiotty_websocket_key_rules.cpp
    src/implementation/websocket/data/repository/send/kiotty_websocket_lane.cpp
    src/implementation/websocket/data/repository/handshake/kiotty_websocket_upgrade.cpp
    src/implementation/websocket/data/repository/handshake/kiotty_websocket_opening.cpp
    src/implementation/websocket/data/repository/frame/kiotty_websocket_frame_reader.cpp
    src/implementation/websocket/data/repository/frame/kiotty_websocket_frame_rules.cpp
    src/implementation/websocket/data/repository/message/kiotty_websocket_message_rules.cpp
    src/implementation/websocket/data/repository/control/kiotty_websocket_control_rules.cpp
    src/implementation/websocket/data/repository/message/kiotty_websocket_message.cpp
    src/implementation/websocket/data/repository/control/kiotty_websocket_control.cpp
    src/implementation/websocket/data/repository/control/kiotty_websocket_timing.cpp
    src/implementation/websocket/data/repository/client/kiotty_websocket_client_machine.cpp
    src/implementation/websocket/data/repository/server/kiotty_websocket_access.cpp
    src/implementation/websocket/data/state/kiotty_websocket_socket_state.cpp
    src/implementation/websocket/data/state/kiotty_websocket_client_state.cpp
    src/implementation/websocket/data/datasource/kiotty_websocket_socket_table.cpp
    src/implementation/http/data/repository/machine/kiotty_http_machine.cpp
    src/implementation/base/data/infra/clock/kiotty_wall_clock.cpp
    src/implementation/http/data/repository/request/kiotty_http_request_store.cpp
    src/implementation/http/data/repository/response/kiotty_http_response_store.cpp
    src/implementation/http/data/repository/server/kiotty_http_registry.cpp
    src/implementation/http/data/repository/body/kiotty_http_file_body.cpp
    src/implementation/http/data/repository/server/kiotty_http_notices.cpp
    src/implementation/http/data/state/kiotty_http_session.cpp
    src/implementation/http/data/datasource/kiotty_http_session_pool.cpp
    src/implementation/http/data/repository/response/kiotty_http_reply_store.cpp
    src/implementation/http/data/repository/header/kiotty_http_shared_headers.cpp
    src/implementation/http/data/repository/server/kiotty_http_configuration.cpp
    src/implementation/http/data/repository/server/kiotty_http_server.cpp
    src/implementation/http/data/repository/server/kiotty_http_work.cpp
    src/implementation/http/data/repository/upgrade/kiotty_http_upgrade_flow.cpp
    src/implementation/websocket/data/repository/server/kiotty_websocket_handler.cpp
    src/implementation/websocket/data/repository/server/kiotty_websocket_server.cpp
    src/implementation/websocket/data/repository/client/kiotty_websocket_client.cpp
    src/implementation/websocket/data/repository/endpoint/kiotty_websocket_endpoint.cpp
    src/implementation/base/domain/usecases/io_loop/kiotty_io_loop_submit.cpp
    src/implementation/base/data/repository/endpoint/kiotty_endpoint.cpp
    src/implementation/base/data/repository/configuration/kiotty_configuration.cpp
    src/implementation/base/data/repository/configuration/kiotty_tls_configuration.cpp
    src/implementation/base/domain/entity/kiotty_action.cpp
    src/implementation/base/data/repository/endpoint/kiotty_run_gate.cpp
    src/implementation/base/data/repository/endpoint/kiotty_run_lifetime.cpp
    src/implementation/base/data/repository/endpoint/kiotty_connection_access.cpp
    src/implementation/base/data/repository/endpoint/kiotty_server.cpp
    src/implementation/base/data/repository/endpoint/kiotty_event_client.cpp
    src/implementation/base/data/repository/endpoint/kiotty_stream_client.cpp
    src/implementation/http/data/repository/client/kiotty_http_client_headers.cpp
    src/implementation/http/data/repository/client/kiotty_http_client_link.cpp
    src/implementation/http/data/repository/client/kiotty_http_client.cpp
    src/implementation/http/data/repository/client/kiotty_http_client_exchange.cpp
    src/implementation/http/data/repository/client/kiotty_http_client_send.cpp
    src/implementation/http/data/repository/client/kiotty_http_client_reply.cpp
    src/implementation/http/data/state/kiotty_http_client_state.cpp
    src/implementation/base/domain/usecases/kiotty_server_usecases.cpp
    src/implementation/base/domain/usecases/kiotty_client_usecases.cpp
    src/implementation/http/domain/usecases/kiotty_http_server_usecases.cpp
    src/implementation/http/domain/usecases/kiotty_http_client_usecases.cpp
    src/implementation/websocket/domain/usecases/kiotty_websocket_server_usecases.cpp
    src/implementation/websocket/domain/usecases/kiotty_websocket_client_usecases.cpp
    src/implementation/context/kiotty_context.cpp
)

add_library(kiotty_net STATIC ${KIOTTY_NET_SOURCES})

target_include_directories(kiotty_net
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/implementation
)

target_compile_features(kiotty_net PUBLIC cxx_std_17 c_std_99)

target_link_libraries(kiotty_net PRIVATE kiotty_net_warnings kiotty_net_hardening)

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
else()
    target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_USE_TLS=0)
endif()

if(WIN32)
    target_link_libraries(kiotty_net PUBLIC ws2_32 ntdll)
endif()

if(KIOTTY_NET_WITH_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT KIOTTY_NET_WITH_LIBKQUEUE)
    include(CheckSymbolExists)
    check_symbol_exists(IORING_ENTER_EXT_ARG "linux/io_uring.h" KIOTTY_NET_HAS_IO_URING_HEADER)
    if(KIOTTY_NET_HAS_IO_URING_HEADER)
        target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_WITH_IO_URING=1)
    else()
        message(STATUS "kiotty_net: linux/io_uring.h is older than 5.11, epoll only")
    endif()
endif()

if(KIOTTY_NET_WITH_LIBKQUEUE)
    find_path(KIOTTY_NET_LIBKQUEUE_INCLUDE_DIR sys/event.h PATH_SUFFIXES kqueue REQUIRED)
    find_library(KIOTTY_NET_LIBKQUEUE_LIBRARY kqueue REQUIRED)
    target_include_directories(kiotty_net SYSTEM PUBLIC ${KIOTTY_NET_LIBKQUEUE_INCLUDE_DIR})
    target_link_libraries(kiotty_net PUBLIC ${KIOTTY_NET_LIBKQUEUE_LIBRARY})
    target_compile_definitions(kiotty_net PUBLIC KIOTTY_NET_WITH_LIBKQUEUE=1)
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
