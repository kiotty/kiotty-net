kiotty_net_add_test_target(kiotty_net_unit_tests unit
    kiotty_test_smoke.cpp
    kiotty_test_headers_cpp.cpp
    kiotty_test_headers_c.c
    kiotty_test_result.cpp
    kiotty_test_result_ref.cpp
    kiotty_test_stage.cpp
    kiotty_test_stage_set.cpp
    kiotty_test_span.cpp
    kiotty_test_fixed_string.cpp
    kiotty_test_text.cpp
    kiotty_test_parse_integer.cpp
    kiotty_test_checked.cpp
    kiotty_test_pool_handle.cpp
    kiotty_test_memory_layout.cpp
    kiotty_test_reusable_pool.cpp
    kiotty_test_lazy_reusable_pool.cpp
    kiotty_test_sized_reusable_pool.cpp
    kiotty_test_parker.cpp
    kiotty_test_clock.cpp
    kiotty_test_socket_error.cpp
    kiotty_test_io_action.cpp
    kiotty_test_tls.cpp
    kiotty_test_sender.cpp
    kiotty_test_work_context.cpp
    kiotty_test_connection.cpp
    kiotty_test_connection_host.cpp
    kiotty_test_connection_tls.cpp
    kiotty_test_configuration.cpp
    kiotty_test_action.cpp
    kiotty_test_rule_table.cpp
    kiotty_test_stateful_handler.cpp
    support/kiotty_test_handler.cpp
    support/kiotty_test_openssl_memory.cpp
)

target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/tests/certs")

if(KIOTTY_NET_USE_TLS)
    target_link_libraries(kiotty_net_unit_tests PRIVATE OpenSSL::Crypto)
    target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_OPENSSL=1)
else()
    target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_OPENSSL=0)
endif()

