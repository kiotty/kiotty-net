#if KIOTTY_NET_USE_TLS

#include "data/infra/tls/kiotty_tls_profile.h"

#include "data/infra/tls/backend/kiotty_openssl_context.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetCode abandonContext(SSL_CTX* context, KiottyNetCode code);
        TlsSettings   clientSettings(const KiottyNetTlsClientConfiguration& config);
    }

    bool tlsSupported()
    {
        return true;
    }

    TlsProfile::~TlsProfile()
    {
        close();
    }

    KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfiguration& config)
    {
        close();
        SSL_CTX* context = SSL_CTX_new(TLS_server_method());
        if (context == nullptr)
        {
            return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
        }
        const KiottyNetCode prepared = prepareServerContext(context, config);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = clientVerifyMode(config.client_authentication);
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _settings = TlsSettings{ TLS_HANDSHAKE_TIMEOUT_MILLISECONDS, true, false };
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfiguration& config)
    {
        close();
        const bool is_server_verified = config.skip_server_verification == 0;
        SSL_CTX* context = SSL_CTX_new(TLS_client_method());
        if (context == nullptr)
        {
            return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
        }
        const KiottyNetCode prepared = prepareClientContext(context, config);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = is_server_verified ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _settings = clientSettings(config);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void TlsProfile::close()
    {
        SSL_CTX_free(_context);
        _context = nullptr;
    }

    namespace
    {
        KiottyNetCode abandonContext(SSL_CTX* context, KiottyNetCode code)
        {
            ERR_clear_error();
            SSL_CTX_free(context);
            return code;
        }

        TlsSettings clientSettings(const KiottyNetTlsClientConfiguration& config)
        {
            const bool is_server_verified = config.skip_server_verification == 0;
            const bool is_hostname_verified = config.skip_hostname_verification == 0;
            const bool has_hostname_verification = is_server_verified && is_hostname_verified;
            const uint32_t timeout_milliseconds = TLS_HANDSHAKE_TIMEOUT_MILLISECONDS;
            return TlsSettings{ timeout_milliseconds, false, has_hostname_verification };
        }
    }
}

#endif
