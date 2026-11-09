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

    void reserveTlsStages(TlsStages stages, TlsCapacity capacity)
    {
        if (!MemoryLimits::TLS) { return; }
        stages.receive.reserve(capacity.receive);
        stages.send.reserve(alignStage(tlsCipherCapacity(capacity.send)));
    }

    TlsEngine::TlsEngine()
        : _cipher_receive()
        , _cipher_send()
        , _ssl(nullptr)
        , _network(nullptr)
        , _status{ TlsFailure{ "", KIOTTY_NET_CODE_SUCCESS }, false }
        , _failed(false)
    {
    }

    DecodeResult TlsEngine::decode(StageBase& plain)
    {
        StageBase::Provider provider = plain.provider();
        const size_t room = provider.writable();
        char* target = provider.prepare(room);
        const DecodeResult decoded = decode(Span<char>{ target, room });
        if (decoded)
        {
            provider.commit(decoded.value);
        }
        return decoded;
    }

    TlsStatus TlsEngine::status() const
    {
        return _status;
    }

    TlsStages TlsEngine::stages()
    {
        return TlsStages{ _cipher_receive, _cipher_send };
    }

    void TlsEngine::limit(TlsCapacity capacity)
    {
        _cipher_receive.limit(capacity.receive);
        _cipher_send.limit(tlsCipherCapacity(capacity.send));
    }

    TlsStep TlsEngine::fail(KiottyNetCode code, const char* message)
    {
        _failed = true;
        _status.failure = TlsFailure{ message, code };
        return TlsStep::FAILED;
    }
}
