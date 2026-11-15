find_package(Python3 COMPONENTS Interpreter QUIET)

set(KIOTTY_NET_SINGLE_HEADER_GENERATOR ${PROJECT_SOURCE_DIR}/tools/make_single_header.py)
set(KIOTTY_NET_SINGLE_HEADER_COMMITTED ${PROJECT_SOURCE_DIR}/single_header/kiotty_net.hpp)
file(GLOB_RECURSE KIOTTY_NET_SINGLE_HEADER_INPUTS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/include/*.h
    ${PROJECT_SOURCE_DIR}/src/*.h
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

function(kiotty_net_generate_single_header variant header)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${variant}/${header})
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${Python3_EXECUTABLE} -I ${KIOTTY_NET_SINGLE_HEADER_GENERATOR} ${ARGN} --output ${output}
        DEPENDS ${KIOTTY_NET_SINGLE_HEADER_GENERATOR}
                ${KIOTTY_NET_SINGLE_HEADER_INPUTS}
                ${PROJECT_SOURCE_DIR}/CMakeLists.txt
        COMMENT "Generating ${variant}/${header}"
        VERBATIM
    )
endfunction()

function(kiotty_net_add_single_header_smoke variant header prefix)
    set(target kiotty_net_single_header_${variant})
    if(Python3_Interpreter_FOUND)
        set(header_directory ${CMAKE_CURRENT_BINARY_DIR}/${variant})
    else()
        set(header_directory ${PROJECT_SOURCE_DIR}/single_header)
    endif()
    add_executable(${target}
        kiotty_test_single_header_main.cpp
        kiotty_test_single_header_second.cpp
        ${variant}/kiotty_test_single_header_implementation.cpp
        ${variant}/kiotty_test_single_header_c.c
        ${header_directory}/${header}
    )
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/${variant}
        ${header_directory}
    )
    target_compile_features(${target} PRIVATE cxx_std_17 c_std_99)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    target_link_libraries(${target} PRIVATE kiotty_net_warnings kiotty_net_hardening Threads::Threads)
    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32 ntdll)
    endif()
    if(KIOTTY_NET_USE_TLS)
        target_compile_definitions(${target} PRIVATE ${prefix}_USE_TLS=1)
        target_link_libraries(${target} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    if(KIOTTY_NET_HAS_IO_URING_HEADER AND KIOTTY_NET_WITH_IO_URING AND NOT KIOTTY_NET_WITH_LIBKQUEUE)
        target_compile_definitions(${target} PRIVATE ${prefix}_WITH_IO_URING=1)
    endif()
    if(KIOTTY_NET_WITH_LIBKQUEUE)
        target_compile_definitions(${target} PRIVATE ${prefix}_WITH_LIBKQUEUE=1)
        target_include_directories(${target} SYSTEM PRIVATE ${KIOTTY_NET_LIBKQUEUE_INCLUDE_DIR})
        target_link_libraries(${target} PRIVATE ${KIOTTY_NET_LIBKQUEUE_LIBRARY})
    endif()
    add_test(NAME single_header_smoke_${variant} COMMAND ${target})
    set_tests_properties(single_header_smoke_${variant} PROPERTIES LABELS integration TIMEOUT 60)
endfunction()

if(Python3_Interpreter_FOUND)
    kiotty_net_generate_single_header(kiotty kiotty_net.hpp)
    kiotty_net_generate_single_header(acme acme_link.hpp --organization Acme --project Link)
    kiotty_net_add_single_header_smoke(kiotty kiotty_net.hpp KIOTTY_NET)
    kiotty_net_add_single_header_smoke(acme acme_link.hpp ACME_LINK)
    add_test(NAME single_header_names_acme
        COMMAND ${CMAKE_COMMAND}
                -DHEADER=${CMAKE_CURRENT_BINARY_DIR}/acme/acme_link.hpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_test_single_header_names.cmake
    )
    set_tests_properties(single_header_names_acme PROPERTIES LABELS unit)
    add_test(NAME single_header_fresh
        COMMAND ${CMAKE_COMMAND}
                -DCOMMITTED=${KIOTTY_NET_SINGLE_HEADER_COMMITTED}
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/kiotty/kiotty_net.hpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_test_single_header_fresh.cmake
    )
    set_tests_properties(single_header_fresh PROPERTIES LABELS fresh)
else()
    message(STATUS "kiotty_net: no Python 3, the single header tests use ${KIOTTY_NET_SINGLE_HEADER_COMMITTED}")
    kiotty_net_add_single_header_smoke(kiotty kiotty_net.hpp KIOTTY_NET)
endif()
