#pragma once

#include "domain/entity/kiotty_socket_code.h"

namespace kiotty::net::implementation
{
    SocketCode startSockets();
    void       stopSockets();
}
