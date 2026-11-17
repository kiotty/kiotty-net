#pragma once

#include "base/data/datasource/kiotty_memory_limits.h"
#include "base/data/infra/socket/kiotty_active_socket.h"
#include "base/data/infra/socket/kiotty_stop_signal.h"
#include "base/data/infra/tls/kiotty_tls_engine.h"
#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "base/data/repository/endpoint/kiotty_endpoint.h"
#include "base/data/repository/stream/kiotty_connection_stream.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "core/pool/kiotty_pool_handle.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using StreamResult = Result<ConnectionStream&, KiottyNetCode>;

    class StreamClient : public Endpoint
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = KiottyNetClientConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_INVALID_ARGUMENT;
        static constexpr KiottyNetCode INVALID_ARGUMENT = KIOTTY_NET_CODE_INVALID_ARGUMENT;

        StreamClient();
        ~StreamClient() override;

        StreamClient(const StreamClient&) = delete;
        StreamClient& operator=(const StreamClient&) = delete;
        StreamClient(StreamClient&&) = delete;
        StreamClient& operator=(StreamClient&&) = delete;

        KiottyNetCode initialize(PoolHandle id, const KiottyNetClientConfiguration& configuration);
        void          reset() noexcept;
        KiottyNetCode connect(const KiottyNetAddress& address,
                              const KiottyNetTlsClientConfiguration* tls);
        KiottyNetCode reconnect(const KiottyNetAddress& address, const Deadline& deadline);
        void          disconnect();
        StreamResult  stream();
        StopSignal&   stop();

        KiottyNetCode           notify(int64_t id, KiottyNetSpan data) override;
        KiottyNetCode           notify(int64_t id, Span<const KiottyNetSpan> pieces) override;
        KiottyNetConnectionInfo info(int64_t id) const override;
        KiottyNetResult         readStream(int64_t id,
                                           Span<char> buffer,
                                           uint32_t timeout_milliseconds) override;
        KiottyNetResult         writeStream(int64_t id,
                                            KiottyNetSpan data,
                                            uint32_t timeout_milliseconds) override;
        KiottyNetCode           writeStreamFile(int64_t id,
                                                Span<const char> path,
                                                FileRange range,
                                                uint32_t timeout_milliseconds) override;
        bool                    isStreamOpen(int64_t id) override;
        KiottyNetCode           closeStream(int64_t id,
                                            const KiottyNetConnectionAction& then) override;

    private:
        KiottyNetCode prepare(const KiottyNetAddress& address,
                              const KiottyNetTlsClientConfiguration* tls);
        KiottyNetCode openProfile(const KiottyNetTlsClientConfiguration* tls);
        KiottyNetCode secure(const KiottyNetAddress& address, const Deadline& deadline);

        Stage                        _receive;
        Stage                        _send;
        TlsEngine                    _tls;
        TlsProfile                   _profile;
        ActiveSocket                 _socket;
        KiottyNetClientConfiguration _configuration;
        StopSignal                   _stop;
        ConnectionStream             _stream;
    };
}
