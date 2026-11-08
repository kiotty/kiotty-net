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

    KiottyNetCode initializeTlsLibrary()
    {
        return KIOTTY_NET_CODE_SUCCESS;
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

    TlsEngine::~TlsEngine()
    {
    }

    KiottyNetCode TlsEngine::open(const TlsProfile& profile, const char* hostname,
                                  TlsCapacity capacity)
    {
        (void)profile;
        (void)hostname;
        (void)capacity;
        return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
    }

    void TlsEngine::close()
    {
        _status.is_active = false;
    }

    TlsStep TlsEngine::handshake()
    {
        return fail(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, NOT_SUPPORTED_MESSAGE);
    }

    DecodeResult TlsEngine::decode(Span<char> plain)
    {
        (void)plain;
        return DecodeResult::failure(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    }

    void TlsEngine::encode(KiottyNetSpan plain)
    {
        (void)plain;
    }

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

#if defined(KIOTTY_NET_TESTING)
    void TlsPipe::requestKeyUpdate()
    {
    }

    void TlsPipe::sendCloseNotify()
    {
    }
#endif
}

#endif
