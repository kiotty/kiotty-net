#if !KIOTTY_NET_USE_TLS

#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_pipe.h"
#include "data/infra/tls/kiotty_tls_profile.h"

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const NOT_SUPPORTED_MESSAGE = "tls: not supported";
    }

    bool tlsSupported()
    {
        return false;
    }

    TlsProfile::TlsProfile()
        : _context(nullptr)
        , _server(false)
        , _verify_hostname(false)
    {
    }

    TlsProfile::~TlsProfile()
    {
    }

    KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfiguration& config)
    {
        (void)config;
        return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
    }

    KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfiguration& config)
    {
        (void)config;
        return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
    }

    void TlsProfile::close()
    {
    }

    bool TlsProfile::valid() const
    {
        return false;
    }

    bool TlsProfile::server() const
    {
        return _server;
    }

    bool TlsProfile::verifyHostname() const
    {
        return _verify_hostname;
    }

    uint32_t TlsProfile::handshakeTimeoutMilliseconds() const
    {
        return TLS_HANDSHAKE_TIMEOUT_MILLISECONDS;
    }

    ssl_ctx_st* TlsProfile::context() const
    {
        return _context;
    }

    TlsEngine::~TlsEngine()
    {
    }

    KiottyNetCode TlsEngine::open(TlsProfile& profile, const char* hostname)
    {
        (void)profile;
        (void)hostname;
        return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
    }

    void TlsEngine::close()
    {
        _active = false;
    }

    TlsStep TlsEngine::handshake()
    {
        return fail(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, NOT_SUPPORTED_MESSAGE);
    }

    DecodeResult TlsEngine::decode(StageBase& plain)
    {
        (void)plain;
        return DecodeResult::failure(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    }

    DecodeResult TlsEngine::decode(char* buffer, size_t length)
    {
        (void)buffer;
        (void)length;
        return DecodeResult::failure(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    }

    void TlsEngine::encode(KiottyNetSpan plain)
    {
        (void)plain;
    }

#if defined(KIOTTY_NET_TESTING)
    void TlsEngine::debugKeyUpdate()
    {
    }

    void TlsEngine::debugShutdown()
    {
    }
#endif

    TlsPipe::TlsPipe(TlsEngine& engine)
        : _engine(engine)
    {
    }

    TlsStep TlsPipe::write(KiottyNetSpan plain)
    {
        (void)plain;
        return _engine.fail(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, NOT_SUPPORTED_MESSAGE);
    }

    Span<char> TlsPipe::receiveRoom()
    {
        return Span<char>{ nullptr, 0 };
    }

    void TlsPipe::received(size_t length)
    {
        (void)length;
    }

    Span<const char> TlsPipe::outgoing()
    {
        return Span<const char>{ nullptr, 0 };
    }

    void TlsPipe::sent(size_t length)
    {
        (void)length;
    }
}

#endif
