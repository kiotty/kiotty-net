kiotty_net_add_test_target(kiotty_net_unit_tests unit
    kiotty_test_smoke.cpp
    kiotty_test_headers_cpp.cpp
    kiotty_test_headers_c.c
    kiotty_test_holder.cpp
    kiotty_test_result.cpp
    kiotty_test_result_ref.cpp
    kiotty_test_stage.cpp
    kiotty_test_stage_set.cpp
    kiotty_test_span.cpp
    kiotty_test_pool_handle.cpp
    kiotty_test_pool.cpp
    kiotty_test_static_pool.cpp
    kiotty_test_memory_layout.cpp
    kiotty_test_reusable_pool.cpp
    kiotty_test_lazy_reusable_pool.cpp
    kiotty_test_parker.cpp
    kiotty_test_clock.cpp
    kiotty_test_socket_error.cpp
    kiotty_test_io_action.cpp
    kiotty_test_tls.cpp
    support/kiotty_test_openssl_memory.cpp
)

target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/tests/certs")

if(KIOTTY_NET_USE_TLS)
    target_link_libraries(kiotty_net_unit_tests PRIVATE OpenSSL::Crypto)
    target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_OPENSSL=1)
else()
    target_compile_definitions(kiotty_net_unit_tests PRIVATE KIOTTY_TEST_OPENSSL=0)
endif()

if(KIOTTY_NET_USE_TLS AND KIOTTY_NET_TLS_STATIC_POOL)
    kiotty_net_add_test_target(kiotty_net_tls_pool_tests unit kiotty_test_tls_static_pool.cpp)
    target_compile_definitions(kiotty_net_tls_pool_tests PRIVATE KIOTTY_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/tests/certs")
endif()
