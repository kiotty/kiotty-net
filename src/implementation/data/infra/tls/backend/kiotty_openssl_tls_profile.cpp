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
        TlsSettings   clientSettings(const KiottyNetTlsClientConfiguration& configuration);
    }

    bool tlsSupported()
    {
        return true;
    }

    KiottyNetCode initializeTlsLibrary()
    {
        const uint64_t options = OPENSSL_INIT_NO_ATEXIT | OPENSSL_INIT_LOAD_SSL_STRINGS;
        const int initialized = OPENSSL_init_ssl(options, nullptr);
        if (initialized != 1) { return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    TlsProfile::~TlsProfile()
    {
        close();
    }

    KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfiguration& configuration)
    {
        close();
        SSL_CTX* context = SSL_CTX_new(TLS_server_method());
        if (context == nullptr)
        {
            return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
        }
        const KiottyNetCode prepared = prepareServerContext(context, configuration);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = clientVerifyMode(configuration.client_authentication);
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _settings = TlsSettings{ TLS_HANDSHAKE_TIMEOUT_MILLISECONDS, true, false };
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfiguration& configuration)
    {
        close();
        const bool is_server_verified =
            configuration.verification != KIOTTY_NET_TLS_VERIFICATION_NONE;
        SSL_CTX* context = SSL_CTX_new(TLS_client_method());
        if (context == nullptr)
        {
            return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
        }
        const KiottyNetCode prepared = prepareClientContext(context, configuration);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = is_server_verified ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _settings = clientSettings(configuration);
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

        TlsSettings clientSettings(const KiottyNetTlsClientConfiguration& configuration)
        {
            const bool has_hostname_verification =
                configuration.verification == KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME;
            const uint32_t timeout_milliseconds = TLS_HANDSHAKE_TIMEOUT_MILLISECONDS;
            return TlsSettings{ timeout_milliseconds, false, has_hostname_verification };
        }
    }
}

#endif
