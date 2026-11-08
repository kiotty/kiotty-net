#if defined(_WIN32)

#include "data/infra/socket/backend/kiotty_windows_address.h"
#include "data/infra/socket/kiotty_socket_startup.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr WORD WINSOCK_VERSION_2_2 = 0x0202;
    }

    SocketCode startSockets()
    {
        WSADATA data{};
        const int started = ::WSAStartup(WINSOCK_VERSION_2_2, &data);
        if (started != 0) { return SocketCode::NOT_SUPPORTED; }
        return SocketCode::SUCCESS;
    }

    void stopSockets()
    {
        (void)::WSACleanup();
    }
}

#endif
