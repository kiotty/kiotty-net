#pragma once

#include "base/domain/entity/kiotty_socket_code.h"

namespace kiotty::net::implementation
{
    SocketCode startSockets();
    void       stopSockets();
}
