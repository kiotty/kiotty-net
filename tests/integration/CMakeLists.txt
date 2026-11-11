kiotty_net_add_test_target(kiotty_net_integration_tests integration
    support/kiotty_test_os.cpp
    support/kiotty_test_peer.cpp
    support/kiotty_test_port_lease.cpp
    support/kiotty_test_platform.cpp
    kiotty_test_active_socket.cpp
    kiotty_test_passive_socket.cpp
    kiotty_test_connect_with_retry.cpp
    kiotty_test_event_listener.cpp
    kiotty_test_connection_stream.cpp
    kiotty_test_acceptor.cpp
    kiotty_test_io_loop.cpp
    kiotty_test_context.cpp
    kiotty_test_endpoint.cpp
    kiotty_test_base_api.cpp
    kiotty_test_stateful.cpp
    kiotty_test_destination.cpp
    kiotty_test_stateful_c.c
    ${PROJECT_SOURCE_DIR}/tests/unit/support/kiotty_test_handler.cpp
)

target_include_directories(kiotty_net_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit)
target_compile_definitions(kiotty_net_integration_tests PRIVATE KIOTTY_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/tests/certs")
