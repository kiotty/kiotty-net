add_executable(kiotty_net_measure_tls_memory kiotty_measure_tls_memory.cpp)
target_include_directories(kiotty_net_measure_tls_memory PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/unit
    ${PROJECT_SOURCE_DIR}/src/implementation
)
target_compile_features(kiotty_net_measure_tls_memory PRIVATE cxx_std_14)
target_compile_definitions(kiotty_net_measure_tls_memory PRIVATE KIOTTY_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/tests/certs")
target_link_libraries(kiotty_net_measure_tls_memory PRIVATE kiotty_net kiotty_net_test_warnings OpenSSL::Crypto)
