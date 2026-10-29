#include "support/kiotty_test_os.h"

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <csignal>
#endif

namespace kiotty_test
{
    namespace
    {
        class PlatformEnvironment : public ::testing::Environment
        {
        public:
            void SetUp() override
            {
#if defined(_WIN32)
                WSADATA data;
                ::WSAStartup(MAKEWORD(2, 2), &data);
#else
                std::signal(SIGPIPE, SIG_IGN);
#endif
            }

            void TearDown() override
            {
#if defined(_WIN32)
                ::WSACleanup();
#endif
            }
        };
    }

    ::testing::Environment* platform_environment = ::testing::AddGlobalTestEnvironment(new PlatformEnvironment);
}
