#include "data/infra/tls/kiotty_tls_engine.h"

namespace kiotty::net::implementation
{
    size_t tlsCipherBound(size_t plain_length)
    {
        size_t records = plain_length / TLS_RECORD_PLAIN_BYTES + 1;
        size_t overhead = records * TLS_RECORD_OVERHEAD_BYTES;
        return plain_length + overhead;
    }

    size_t tlsCipherCapacity(size_t plain_capacity)
    {
        if (plain_capacity == 0)
        {
            return 0;
        }
        size_t bound = tlsCipherBound(plain_capacity);
        return bound + TLS_CONTROL_RESERVE_BYTES;
    }

    TlsEngine::TlsEngine()
        : _cipher_receive()
        , _cipher_send()
        , _ssl(nullptr)
        , _network(nullptr)
        , _failure{ "", KIOTTY_NET_CODE_SUCCESS }
        , _active(false)
        , _failed(false)
    {
    }

    bool TlsEngine::active() const
    {
        return _active;
    }

    void TlsEngine::limit(size_t receive_capacity, size_t send_capacity)
    {
        _cipher_receive.limit(receive_capacity);
        _cipher_send.limit(tlsCipherCapacity(send_capacity));
    }

    StageBase& TlsEngine::cipherReceive()
    {
        return _cipher_receive;
    }

    StageBase& TlsEngine::cipherSend()
    {
        return _cipher_send;
    }

    TlsFailure TlsEngine::failure() const
    {
        return _failure;
    }

    TlsStep TlsEngine::fail(KiottyNetCode code, const char* message)
    {
        _failed = true;
        _failure = TlsFailure{ message, code };
        return TlsStep::FAILED;
    }
}
