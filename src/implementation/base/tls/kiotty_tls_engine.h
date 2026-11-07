#if !defined(KIOTTY_TLS_ENGINE_H)
#define KIOTTY_TLS_ENGINE_H

#include "base/tls/kiotty_tls_profile.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "data/datasource/kiotty_memory_layout.h"
#include "core/kiotty_stage.h"
#include "domain/entity/kiotty_tls_step.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct ssl_st;
struct bio_st;

namespace kiotty::net::implementation
{
    typedef Result<size_t, KiottyNetCode> DecodeResult;

    size_t tlsCipherBound(size_t plain_length);
    size_t tlsCipherCapacity(size_t plain_capacity);

    class TlsEngine
    {
    public:
        TlsEngine();
        ~TlsEngine();

        TlsEngine(const TlsEngine&) = delete;
        TlsEngine& operator=(const TlsEngine&) = delete;

        KiottyNetCode open(TlsProfile& profile, const char* hostname);
        void          close();
        bool          active() const;
        void          limit(size_t receive_capacity, size_t send_capacity);

        StageBase& cipherReceive();
        StageBase& cipherSend();

        TlsStep      handshake();
        DecodeResult decode(StageBase& plain);
        DecodeResult decode(char* buffer, size_t length);
        void         encode(KiottyNetSpan plain);
        TlsStep      write(KiottyNetSpan plain);

        Span<char>       receiveRoom();
        void             received(size_t length);
        Span<const char> outgoing();
        void             sent(size_t length);

        KiottyNetCode lastCode() const;
        const char*   lastMessage() const;

#if defined(KIOTTY_NET_TESTING)
        void debugKeyUpdate();
        void debugShutdown();
#endif

    private:
        TlsStep fail(KiottyNetCode code, const char* message);
        size_t  feed();
        size_t  flush();
        size_t  pending() const;

        Stage<CIPHER_RECEIVE_BYTES> _cipher_receive;
        Stage<CIPHER_SEND_BYTES>    _cipher_send;
        ssl_st*       _ssl;
        bio_st*       _network;
        bool          _active;
        bool          _failed;
        KiottyNetCode _last_code;
        const char*   _last_message;
    };
}

#endif
