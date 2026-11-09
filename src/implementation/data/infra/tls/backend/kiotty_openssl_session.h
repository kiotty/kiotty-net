#pragma once

#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_profile.h"

#include <openssl/ssl.h>

namespace kiotty::net::implementation
{
    inline constexpr const char* NOT_OPEN_MESSAGE = "tls: engine is not open";

    SSL*          reuseSsl(SSL* ssl, SSL_CTX* context);
    void          startRole(SSL* ssl, TlsSettings settings);
    KiottyNetCode setHostname(SSL* ssl, TlsSettings settings, Span<const char> hostname);
    bool          isRetryable(int reason);
    TlsFailure    handshakeFailure(const SSL* ssl);
    TlsFailure    readFailure(int reason);
    void          answerKeyUpdate(SSL* ssl);
    const char*   lastErrorMessage();
}
