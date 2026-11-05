#if !KIOTTY_NET_USE_TLS

#include "base/tls/kiotty_tls_engine.h"
#include "base/tls/kiotty_tls_profile.h"

namespace kiotty
{
    namespace net
    {
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

        KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfig& config)
        {
            (void)config;
            return KIOTTY_TLS_NOT_SUPPORTED;
        }

        KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfig& config)
        {
            (void)config;
            return KIOTTY_TLS_NOT_SUPPORTED;
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

        uint32_t TlsProfile::handshakeTimeoutMs() const
        {
            return TLS_HANDSHAKE_TIMEOUT_MS;
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
            return KIOTTY_TLS_NOT_SUPPORTED;
        }

        void TlsEngine::close()
        {
            _active = false;
        }

        TlsStep TlsEngine::handshake()
        {
            return fail(KIOTTY_TLS_NOT_SUPPORTED, "tls: not supported");
        }

        DecodeResult TlsEngine::decode(StageBase& plain)
        {
            (void)plain;
            return DecodeResult::failure(KIOTTY_TLS_NOT_SUPPORTED);
        }

        DecodeResult TlsEngine::decode(char* buffer, size_t length)
        {
            (void)buffer;
            (void)length;
            return DecodeResult::failure(KIOTTY_TLS_NOT_SUPPORTED);
        }

        void TlsEngine::encode(KiottyNetSpan plain)
        {
            (void)plain;
        }

        TlsStep TlsEngine::write(KiottyNetSpan plain)
        {
            (void)plain;
            return fail(KIOTTY_TLS_NOT_SUPPORTED, "tls: not supported");
        }

        Span<char> TlsEngine::receiveRoom()
        {
            Span<char> room = { nullptr, 0 };
            return room;
        }

        void TlsEngine::received(size_t length)
        {
            (void)length;
        }

        Span<const char> TlsEngine::outgoing()
        {
            Span<const char> waiting = { nullptr, 0 };
            return waiting;
        }

        void TlsEngine::sent(size_t length)
        {
            (void)length;
        }

#if defined(KIOTTY_NET_TESTING)
        void TlsEngine::debugKeyUpdate()
        {
        }

        void TlsEngine::debugShutdown()
        {
        }
#endif
    }
}

#endif
