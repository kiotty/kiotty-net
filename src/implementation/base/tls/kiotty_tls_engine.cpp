#include "base/tls/kiotty_tls_engine.h"

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const size_t RECORD_PLAIN_BYTES = 16384;
            const size_t RECORD_OVERHEAD_BYTES = 64;
            const size_t CONTROL_RESERVE_BYTES = 512;
        }

        size_t tlsCipherBound(size_t plain_length)
        {
            size_t records = plain_length / RECORD_PLAIN_BYTES + 1;
            size_t overhead = records * RECORD_OVERHEAD_BYTES;
            return plain_length + overhead;
        }

        size_t tlsCipherCapacity(size_t plain_capacity)
        {
            if (plain_capacity == 0)
            {
                return 0;
            }
            size_t bound = tlsCipherBound(plain_capacity);
            return bound + CONTROL_RESERVE_BYTES;
        }

        TlsEngine::TlsEngine(Pool& pool, size_t receive_capacity, size_t send_capacity)
            : _cipher_receive(pool, receive_capacity)
            , _cipher_send(pool, tlsCipherCapacity(send_capacity))
            , _ssl(nullptr)
            , _network(nullptr)
            , _active(false)
            , _failed(false)
            , _last_code(KIOTTY_SUCCESS)
            , _last_message("")
        {
        }

        bool TlsEngine::active() const
        {
            return _active;
        }

        Stage& TlsEngine::cipherReceive()
        {
            return _cipher_receive;
        }

        Stage& TlsEngine::cipherSend()
        {
            return _cipher_send;
        }

        KiottyNetCode TlsEngine::lastCode() const
        {
            return _last_code;
        }

        const char* TlsEngine::lastMessage() const
        {
            return _last_message;
        }

        TlsStep TlsEngine::fail(KiottyNetCode code, const char* message)
        {
            _failed = true;
            _last_code = code;
            _last_message = message;
            return TlsStep::FAILED;
        }
    }
}
