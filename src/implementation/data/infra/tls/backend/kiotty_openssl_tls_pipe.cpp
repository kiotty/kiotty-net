#if KIOTTY_NET_USE_TLS

#include "data/infra/tls/kiotty_tls_pipe.h"

#include "data/infra/tls/backend/kiotty_openssl_session.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace kiotty::net::implementation
{
    TlsPipe::TlsPipe(TlsEngine& engine)
        : _engine(engine)
    {
    }

    TlsStep TlsPipe::write(KiottyNetSpan plain)
    {
        if (!_engine._status.is_active || _engine._failed)
        {
            return _engine.fail(KIOTTY_NET_CODE_INVALID_ARGUMENT, NOT_OPEN_MESSAGE);
        }
        if (plain.length == 0) { return TlsStep::DONE; }
        size_t written = 0;
        ERR_clear_error();
        const int result = SSL_write_ex(_engine._ssl, plain.data, plain.length, &written);
        if (result == 1) { return TlsStep::DONE; }
        const int reason = SSL_get_error(_engine._ssl, result);
        if (reason == SSL_ERROR_WANT_WRITE) { return TlsStep::SEND; }
        const char* message = lastErrorMessage();
        return _engine.fail(KIOTTY_NET_CODE_TLS_RECORD_ERROR, message);
    }

    Span<char> TlsPipe::receiveRoom()
    {
        const Span<char> none{ nullptr, 0 };
        if (!_engine._status.is_active) { return none; }
        _engine.feed();
        StageBase::Consumer waiting = _engine._cipher_receive.consumer();
        const size_t readable = waiting.readable();
        if (readable > 0) { return none; }
        char* target = nullptr;
        const ossl_ssize_t available = BIO_nwrite0(_engine._network, &target);
        if (available <= 0) { return none; }
        return Span<char>{ target, static_cast<size_t>(available) };
    }

    void TlsPipe::received(size_t length)
    {
        if (length == 0) { return; }
        char* target = nullptr;
        BIO_nwrite(_engine._network, &target, static_cast<int>(length));
    }

    Span<const char> TlsPipe::outgoing()
    {
        const Span<const char> none{ nullptr, 0 };
        if (!_engine._status.is_active) { return none; }
        char* source = nullptr;
        const ossl_ssize_t available = BIO_nread0(_engine._network, &source);
        if (available <= 0) { return none; }
        return Span<const char>{ source, static_cast<size_t>(available) };
    }

    void TlsPipe::sent(size_t length)
    {
        if (length == 0) { return; }
        char* source = nullptr;
        BIO_nread(_engine._network, &source, static_cast<int>(length));
    }

#if defined(KIOTTY_NET_TESTING)
    void TlsPipe::requestKeyUpdate()
    {
        SSL_key_update(_engine._ssl, SSL_KEY_UPDATE_REQUESTED);
        SSL_do_handshake(_engine._ssl);
        _engine.flush();
    }

    void TlsPipe::sendCloseNotify()
    {
        SSL_shutdown(_engine._ssl);
        _engine.flush();
    }
#endif
}

#endif
