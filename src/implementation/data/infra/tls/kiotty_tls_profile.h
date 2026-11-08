#pragma once

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct ssl_ctx_st;

namespace kiotty::net::implementation
{
    inline constexpr uint32_t TLS_HANDSHAKE_TIMEOUT_MILLISECONDS = 10000;

    bool          tlsSupported();
    KiottyNetCode initializeTlsLibrary();

    struct TlsSettings
    {
        uint32_t handshake_timeout_milliseconds;
        bool     is_server;
        bool     has_hostname_verification;
    };

    static_assert(sizeof(TlsSettings) == 8, "TlsSettings: unexpected padding");

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
        TlsSettings settings() const;
        ssl_ctx_st* context() const;

    private:
        ssl_ctx_st* _context;
        TlsSettings _settings;
    };
}
