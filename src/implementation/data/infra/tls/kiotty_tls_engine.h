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

    static_assert(sizeof(TlsFailure) == 2 * sizeof(void*), "TlsFailure: unexpected padding");

    struct TlsStatus
    {
        TlsFailure failure;
        bool       is_active;
    };

    static_assert(sizeof(TlsStatus) == sizeof(TlsFailure) + sizeof(void*),
                  "TlsStatus: unexpected padding");

    struct TlsCapacity
    {
        size_t receive;
        size_t send;
    };

    static_assert(sizeof(TlsCapacity) == 2 * sizeof(size_t), "TlsCapacity: unexpected padding");

    struct TlsStages
    {
        StageBase& receive;
        StageBase& send;
    };

    static_assert(sizeof(TlsStages) == 2 * sizeof(void*), "TlsStages: unexpected padding");

    class TlsPipe;

    class TlsEngine
    {
    public:
        TlsEngine();
        ~TlsEngine();

        TlsEngine(const TlsEngine&) = delete;
        TlsEngine& operator=(const TlsEngine&) = delete;

        KiottyNetCode open(const TlsProfile& profile, const char* hostname, TlsCapacity capacity);
        void          close();
        TlsStep       handshake();
        DecodeResult  decode(StageBase& plain);
        DecodeResult  decode(Span<char> plain);
        void          encode(KiottyNetSpan plain);
        TlsStatus     status() const;
        TlsStages     stages();

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

        void          limit(TlsCapacity capacity);
        TlsStep       fail(KiottyNetCode code, const char* message);
        KiottyNetCode prepareSsl(ssl_ctx_st* context);
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
        ssl_st*                     _ssl;
        bio_st*                     _network;
        TlsStatus                   _status;
        bool                        _failed;
    };
}
