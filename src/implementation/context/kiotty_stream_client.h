#pragma once

#include "context/kiotty_client_configuration.h"
#include "context/kiotty_endpoint.h"
#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "core/pool/kiotty_pool_handle.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "data/repository/stream/kiotty_connection_stream.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    class StreamClient : public Endpoint
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = ClientConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_INVALID_ARGUMENT;
        static constexpr KiottyNetCode INVALID_ARGUMENT = KIOTTY_NET_CODE_INVALID_ARGUMENT;

        StreamClient();
        ~StreamClient() override;

        StreamClient(const StreamClient&) = delete;
        StreamClient& operator=(const StreamClient&) = delete;
        StreamClient(StreamClient&&) = delete;
        StreamClient& operator=(StreamClient&&) = delete;

        KiottyNetCode   initialize(PoolHandle id, ClientConfiguration configuration);
        void            reset() noexcept;
        KiottyNetCode   connect();
        void            disconnect();
        KiottyNetResult read(Span<char> buffer, uint32_t timeout_milliseconds);
        KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);

        KiottyNetCode           notify(int64_t id, KiottyNetSpan data) override;
        KiottyNetConnectionInfo info(int64_t id) const override;
        KiottyNetResult         readStream(int64_t id,
                                           Span<char> buffer,
                                           uint32_t timeout_milliseconds) override;
        KiottyNetResult         writeStream(int64_t id,
                                            KiottyNetSpan data,
                                            uint32_t timeout_milliseconds) override;
        KiottyNetCode           writeStreamFile(int64_t id,
                                                const char* path,
                                                FileRange range,
                                                uint32_t timeout_milliseconds) override;
        bool                    isStreamOpen(int64_t id) override;
        KiottyNetCode           closeStream(int64_t id,
                                            const KiottyNetConnectionAction& then) override;

    private:
        KiottyNetCode    openProfile(const KiottyNetTlsClientConfiguration* tls);
        KiottyNetCode    secure();
        ConnectionStream stream();

        Stage                        _receive;
        Stage                        _send;
        TlsEngine                    _tls;
        TlsProfile                   _profile;
        ActiveSocket                 _socket;
        KiottyNetClientConfiguration _configuration;
        std::atomic<bool>            _is_stopping;
    };
}
