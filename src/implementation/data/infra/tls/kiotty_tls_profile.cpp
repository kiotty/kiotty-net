#include "data/infra/tls/kiotty_tls_profile.h"

namespace kiotty::net::implementation
{
    TlsProfile::TlsProfile()
        : _context(nullptr)
        , _settings{ TLS_HANDSHAKE_TIMEOUT_MILLISECONDS, false, false }
    {
    }

    bool TlsProfile::valid() const
    {
        return _context != nullptr;
    }

    TlsSettings TlsProfile::settings() const
    {
        return _settings;
    }

    ssl_ctx_st* TlsProfile::context() const
    {
        return _context;
    }
}
