#if KIOTTY_NET_USE_TLS

#include "data/infra/tls/kiotty_tls_engine.h"

#include "data/infra/tls/backend/kiotty_openssl_session.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cassert>

namespace kiotty::net::implementation
{
    namespace
    {
        const size_t BIO_PAIR_BYTES = 4096;

        const char* const HANDSHAKE_CLOSED_MESSAGE = "tls: peer closed during handshake";
    }

    TlsEngine::~TlsEngine()
    {
        SSL_free(_ssl);
        BIO_free(_network);
    }

    KiottyNetCode TlsEngine::open(const TlsProfile& profile, Span<const char> hostname,
                                  TlsCapacity capacity)
    {
        close();
        limit(capacity);
        const bool has_capacity = _cipher_receive.capacity() > 0 && _cipher_send.capacity() > 0;
        if (!has_capacity) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_valid = profile.valid();
        if (!is_valid) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode prepared = prepareSsl(profile.context());
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        const TlsSettings settings = profile.settings();
        startRole(_ssl, settings);
        const KiottyNetCode named = setHostname(_ssl, settings, hostname);
        if (named != KIOTTY_NET_CODE_SUCCESS) { return named; }
        _status = TlsStatus{ TlsFailure{ "", KIOTTY_NET_CODE_SUCCESS }, true };
        _failed = false;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void TlsEngine::close()
    {
        _status.is_active = false;
        _cipher_receive.clear();
        _cipher_send.clear();
    }

    TlsStep TlsEngine::handshake()
    {
        if (!_status.is_active) { return fail(KIOTTY_NET_CODE_INVALID_ARGUMENT, NOT_OPEN_MESSAGE); }
        if (_failed) { return TlsStep::FAILED; }
        const Outcome outcome = driveHandshake();
        const bool is_complete = outcome.result == 1;
        const bool is_closed = outcome.reason == SSL_ERROR_ZERO_RETURN;
        if (!is_complete && is_closed)
        {
            return fail(KIOTTY_NET_CODE_PEER_CLOSED, HANDSHAKE_CLOSED_MESSAGE);
        }
        const bool is_retryable = isRetryable(outcome.reason);
        if (!is_complete && !is_retryable)
        {
            const TlsFailure rejected = handshakeFailure(_ssl);
            return fail(rejected.code, rejected.message);
        }
        StageBase::Consumer waiting = _cipher_send.consumer();
        const size_t readable = waiting.readable();
        if (readable > 0) { return TlsStep::SEND; }
        if (is_complete) { return TlsStep::DONE; }
        return TlsStep::RECEIVE;
    }

    DecodeResult TlsEngine::decode(Span<char> plain)
    {
        if (!_status.is_active || _failed)
        {
            _status.failure = TlsFailure{ NOT_OPEN_MESSAGE, KIOTTY_NET_CODE_INVALID_ARGUMENT };
            return DecodeResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        feed();
        const Drained drained = drain(plain);
        answerKeyUpdate(_ssl);
        flush();
        if (drained.code != KIOTTY_NET_CODE_SUCCESS && drained.length == 0)
        {
            _status.failure.code = drained.code;
            return DecodeResult::failure(drained.code);
        }
        return DecodeResult::success(drained.length);
    }

    void TlsEngine::encode(KiottyNetSpan plain)
    {
        if (plain.length == 0) { return; }
        assert(_status.is_active && !_failed);
        if (!_status.is_active || _failed) { return; }
        const bool fits = fitsCipherSend(plain.length);
        assert(fits);
        if (!fits) { return; }
        writeRecord(plain);
        flush();
    }

    KiottyNetCode TlsEngine::prepareSsl(ssl_ctx_st* context)
    {
        _ssl = reuseSsl(_ssl, context);
        if (_ssl == nullptr)
        {
            const KiottyNetCode created = createSsl(context);
            if (created != KIOTTY_NET_CODE_SUCCESS) { return created; }
        }
        BIO* internal = SSL_get_rbio(_ssl);
        BIO_reset(internal);
        BIO_reset(_network);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode TlsEngine::createSsl(ssl_ctx_st* context)
    {
        BIO_free(_network);
        _network = nullptr;
        _ssl = SSL_new(context);
        BIO* internal = nullptr;
        const int paired = BIO_new_bio_pair(&internal, BIO_PAIR_BYTES, &_network, BIO_PAIR_BYTES);
        if (_ssl == nullptr || paired != 1)
        {
            BIO_free(internal);
            BIO_free(_network);
            _network = nullptr;
            SSL_free(_ssl);
            _ssl = nullptr;
            ERR_clear_error();
            return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
        }
        SSL_set_bio(_ssl, internal, internal);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    TlsEngine::Outcome TlsEngine::driveHandshake()
    {
        Outcome outcome{ 0, SSL_ERROR_NONE };
        bool is_progressing = true;
        while (is_progressing)
        {
            const size_t fed = feed();
            ERR_clear_error();
            outcome.result = SSL_do_handshake(_ssl);
            outcome.reason = SSL_get_error(_ssl, outcome.result);
            const size_t flushed = flush();
            const bool is_retry = outcome.result != 1 && isRetryable(outcome.reason);
            is_progressing = is_retry && (fed > 0 || flushed > 0);
        }
        return outcome;
    }

    TlsEngine::Drained TlsEngine::drain(Span<char> target)
    {
        Drained drained{ 0, KIOTTY_NET_CODE_SUCCESS };
        while (drained.length < target.length)
        {
            const Span<char> rest = target.skip(drained.length);
            size_t read = 0;
            ERR_clear_error();
            const int result = SSL_read_ex(_ssl, rest.data, rest.length, &read);
            if (result == 1)
            {
                drained.length += read;
                continue;
            }
            const int reason = SSL_get_error(_ssl, result);
            const size_t resumed = resume(reason);
            if (resumed > 0) { continue; }
            const bool is_retryable = isRetryable(reason);
            if (is_retryable) { return drained; }
            const TlsFailure stopped = readFailure(reason);
            _status.failure.message = stopped.message;
            drained.code = stopped.code;
            return drained;
        }
        return drained;
    }

    size_t TlsEngine::resume(int reason)
    {
        if (reason == SSL_ERROR_WANT_READ) { return feed(); }
        if (reason == SSL_ERROR_WANT_WRITE) { return flush(); }
        return 0;
    }

    bool TlsEngine::fitsCipherSend(size_t plain_length)
    {
        StageBase::Consumer waiting = _cipher_send.consumer();
        const size_t readable = waiting.readable();
        if (readable == 0)
        {
            _cipher_send.compact();
        }
        StageBase::Provider provider = _cipher_send.provider();
        const size_t bound = tlsCipherBound(plain_length);
        const size_t need = pending() + bound;
        return need <= provider.writable();
    }

    void TlsEngine::writeRecord(KiottyNetSpan plain)
    {
        size_t written = 0;
        int result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
        int reason = SSL_get_error(_ssl, result);
        while (result != 1 && reason == SSL_ERROR_WANT_WRITE)
        {
            const size_t flushed = flush();
            if (flushed == 0) { break; }
            result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
            reason = SSL_get_error(_ssl, result);
        }
        assert(result == 1 && written == plain.length);
        if (result != 1)
        {
            ERR_clear_error();
        }
    }

    size_t TlsEngine::feed()
    {
        StageBase::Consumer consumer = _cipher_receive.consumer();
        const size_t readable = consumer.readable();
        const size_t room = BIO_ctrl_get_write_guarantee(_network);
        const size_t length = readable < room ? readable : room;
        size_t written = 0;
        if (length > 0)
        {
            BIO_write_ex(_network, consumer.peek(), length, &written);
            consumer.commit(written);
        }
        _cipher_receive.compact();
        return written;
    }

    size_t TlsEngine::flush()
    {
        StageBase::Consumer waiting = _cipher_send.consumer();
        const size_t readable = waiting.readable();
        if (readable == 0)
        {
            _cipher_send.compact();
        }
        StageBase::Provider provider = _cipher_send.provider();
        const size_t room = provider.writable();
        const size_t queued = pending();
        const size_t length = room < queued ? room : queued;
        if (length == 0) { return 0; }
        char* target = provider.prepare(length);
        size_t read = 0;
        BIO_read_ex(_network, target, length, &read);
        provider.commit(read);
        return read;
    }

    size_t TlsEngine::pending() const
    {
        return BIO_ctrl_pending(_network);
    }
}

#endif
