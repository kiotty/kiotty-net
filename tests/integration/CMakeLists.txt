set(KIOTTY_NET_INTEGRATION_SOURCES
    support/kiotty_test_os.cpp
    support/kiotty_test_peer.cpp
    support/kiotty_test_platform.cpp
    kiotty_test_active_socket.cpp
    kiotty_test_passive_socket.cpp
    kiotty_test_connect_with_retry.cpp
)

if(KIOTTY_NET_HAS_EVENT_BACKEND)
    list(APPEND KIOTTY_NET_INTEGRATION_SOURCES
        kiotty_test_event_listener.cpp
    )
endif()

kiotty_net_add_test_target(kiotty_net_integration_tests integration ${KIOTTY_NET_INTEGRATION_SOURCES})
