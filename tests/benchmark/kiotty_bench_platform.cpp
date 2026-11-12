#include "support/kiotty_test_os.h"

#if !defined(_WIN32)
#include <csignal>
#endif

namespace
{
    class PlatformStartup
    {
    public:
        PlatformStartup()
        {
#if defined(_WIN32)
            WSADATA data;
            ::WSAStartup(MAKEWORD(2, 2), &data);
#else
            std::signal(SIGPIPE, SIG_IGN);
#endif
        }

        ~PlatformStartup()
        {
#if defined(_WIN32)
            ::WSACleanup();
#endif
        }

        PlatformStartup(const PlatformStartup&) = delete;
        PlatformStartup& operator=(const PlatformStartup&) = delete;
    };

    const PlatformStartup platform_startup;
}
