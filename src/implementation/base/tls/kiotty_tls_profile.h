#if !defined(KIOTTY_TLS_PROFILE_H)
#define KIOTTY_TLS_PROFILE_H

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct ssl_ctx_st;

namespace kiotty::net::implementation
{
    static const uint32_t TLS_HANDSHAKE_TIMEOUT_MS = 10000;

    bool tlsSupported();

    class TlsProfile
    {
    public:
        TlsProfile();
        ~TlsProfile();

        TlsProfile(const TlsProfile&) = delete;
        TlsProfile& operator=(const TlsProfile&) = delete;

        KiottyNetCode openServer(const KiottyNetTlsServerConfiguration& config);
        KiottyNetCode openClient(const KiottyNetTlsClientConfiguration& config);
        void          close();

        bool        valid() const;
        bool        server() const;
        bool        verifyHostname() const;
        uint32_t    handshakeTimeoutMilliseconds() const;
        ssl_ctx_st* context() const;

    private:
        ssl_ctx_st* _context;
        bool        _server;
        bool        _verify_hostname;
    };
}

#endif
