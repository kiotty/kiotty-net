#pragma once

#include "data/infra/tls/kiotty_tls_profile.h"
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

    struct TlsFailure
    {
        const char*   message;
        KiottyNetCode code;
    };

    class TlsPipe;

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

        TlsFailure failure() const;

#if defined(KIOTTY_NET_TESTING)
        void debugKeyUpdate();
        void debugShutdown();
#endif

    private:
        friend class TlsPipe;

        struct Outcome
        {
            int result;
            int reason;
        };

        struct Drained
        {
            size_t        length;
            KiottyNetCode code;
        };

        TlsStep       fail(KiottyNetCode code, const char* message);
        KiottyNetCode createSsl(ssl_ctx_st* context);
        Outcome       driveHandshake();
        Drained       drain(Span<char> target);
        size_t        resume(int reason);
        bool          fitsCipherSend(size_t plain_length);
        void          writeRecord(KiottyNetSpan plain);
        size_t        feed();
        size_t        flush();
        size_t        pending() const;

        Stage<CIPHER_RECEIVE_BYTES> _cipher_receive;
        Stage<CIPHER_SEND_BYTES>    _cipher_send;
        ssl_st*       _ssl;
        bio_st*       _network;
        TlsFailure    _failure;
        bool          _active;
        bool          _failed;
    };
}
