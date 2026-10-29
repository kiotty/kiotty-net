include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(kiotty_net_add_test_target target label)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/implementation
    )
    target_compile_features(${target} PRIVATE cxx_std_14)
    target_compile_definitions(${target} PRIVATE KIOTTY_NET_TESTING=1)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++17-attribute-extensions>
    )
    target_link_libraries(${target} PRIVATE kiotty_net kiotty_net_warnings GTest::gtest_main)
    gtest_discover_tests(${target}
        PROPERTIES LABELS ${label}
        DISCOVERY_MODE PRE_TEST
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)

if(KIOTTY_NET_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
