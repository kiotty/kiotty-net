#pragma once

#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct ClientConfiguration
    {
        KiottyNetClientConfiguration           client;
        const KiottyNetTlsClientConfiguration* tls;
    };
}
