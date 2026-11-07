#pragma once

#include "kiotty_net_base.h"

#include <openssl/ssl.h>

namespace kiotty::net::implementation
{
    KiottyNetCode prepareServerContext(SSL_CTX* context,
                                       const KiottyNetTlsServerConfiguration& config);
    KiottyNetCode prepareClientContext(SSL_CTX* context,
                                       const KiottyNetTlsClientConfiguration& config);
    int           clientVerifyMode(KiottyNetClientAuthentication authentication);
}
