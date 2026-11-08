#if !defined(_WIN32)

#include "data/infra/socket/kiotty_socket_startup.h"

namespace kiotty::net::implementation
{
    SocketCode startSockets()
    {
        return SocketCode::SUCCESS;
    }

    void stopSockets()
    {
    }
}

#endif
