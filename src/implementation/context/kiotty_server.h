#pragma once

#include "context/kiotty_connection_access.h"
#include "context/kiotty_endpoint.h"
#include "context/kiotty_run_gate.h"
#include "context/kiotty_run_lifetime.h"
#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_sized_reusable_pool.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "data/repository/connection/kiotty_acceptor.h"
#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_host.h"
#include "data/repository/connection/kiotty_notification_desk.h"
#include "domain/usecases/io_loop/kiotty_io_loop.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct ServerConfiguration
    {
        KiottyNetServerConfiguration           server;
        const KiottyNetTlsServerConfiguration* tls;
    };

    class Server : public Endpoint
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = ServerConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_INVALID_ARGUMENT;
        static constexpr KiottyNetCode INVALID_ARGUMENT = KIOTTY_NET_CODE_INVALID_ARGUMENT;

        Server();
        ~Server() override;

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;
        Server(Server&&) = delete;
        Server& operator=(Server&&) = delete;

        KiottyNetCode initialize(PoolHandle id, ServerConfiguration configuration);
        void          reset() noexcept;
        KiottyNetCode run(const KiottyNetConnectionHandler& callbacks,
                          KiottyNetAppContext app_context);
        void          close();
        KiottyNetCode await(uint64_t timeout_milliseconds);
        KiottyNetCode broadcast(KiottyNetSpan data);
        RunLifetime   lifetime();

        KiottyNetCode           notify(int64_t id, KiottyNetSpan data) override;
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
        static constexpr int LISTEN_BACKLOG = 128;

        KiottyNetCode    openProfile(const KiottyNetTlsServerConfiguration* tls);
        KiottyNetCode    listen();
        KiottyNetCode    leave(KiottyNetCode code);
        ConnectionAccess access();

        SizedReusablePool<Connection> _connections;
        ConnectionHost                _host;
        NotificationDesk              _desk;
        EventListener                 _listener;
        Acceptor                      _acceptor;
        TlsProfile                    _profile;
        IoLoop                        _loop;
        RunGate                       _gate;
        std::atomic<bool>             _is_abandoned;
        KiottyNetServerConfiguration  _configuration;
    };
}
