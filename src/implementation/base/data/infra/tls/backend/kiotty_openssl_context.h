#pragma once

#include "kiotty_net_base.h"

#include <openssl/ssl.h>

namespace kiotty::net::implementation
{
    KiottyNetCode prepareServerContext(SSL_CTX* context,
                                       const KiottyNetTlsServerConfiguration& configuration);
    KiottyNetCode prepareClientContext(SSL_CTX* context,
                                       const KiottyNetTlsClientConfiguration& configuration);
    int           clientVerifyMode(KiottyNetClientAuthentication authentication);
}
