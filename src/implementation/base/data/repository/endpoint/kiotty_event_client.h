#pragma once

#include "base/data/infra/event/kiotty_event_listener.h"
#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "base/data/repository/connection/kiotty_acceptor.h"
#include "base/data/repository/connection/kiotty_connection.h"
#include "base/data/repository/connection/kiotty_connection_host.h"
#include "base/data/repository/connection/kiotty_notification_desk.h"
#include "base/data/repository/endpoint/kiotty_connection_access.h"
#include "base/data/repository/endpoint/kiotty_endpoint.h"
#include "base/data/repository/endpoint/kiotty_run_gate.h"
#include "base/data/repository/endpoint/kiotty_run_lifetime.h"
#include "base/domain/usecases/io_loop/kiotty_io_loop.h"
#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class EventClient : public Endpoint
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = KiottyNetClientConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_INVALID_ARGUMENT;
        static constexpr KiottyNetCode INVALID_ARGUMENT = KIOTTY_NET_CODE_INVALID_ARGUMENT;

        EventClient();
        ~EventClient() override;

        EventClient(const EventClient&) = delete;
        EventClient& operator=(const EventClient&) = delete;
        EventClient(EventClient&&) = delete;
        EventClient& operator=(EventClient&&) = delete;

        KiottyNetCode             initialize(PoolHandle id,
                                             const KiottyNetClientConfiguration& configuration);
        void                      reset() noexcept;
        KiottyNetCode             run(const Handler& handler,
                                      const KiottyNetAddress& address,
                                      const KiottyNetTlsClientConfiguration* tls);
        void                      close();
        KiottyNetCode             await(uint64_t timeout_milliseconds);
        KiottyNetConnectionHandle connection();
        RunLifetime               lifetime();

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
        static constexpr size_t CONNECTIONS = 1;
        static constexpr size_t LISTENER_CAPACITY = 2 * CONNECTIONS + 2;

        KiottyNetCode    openProfile(const KiottyNetTlsClientConfiguration* tls);
        KiottyNetCode    leave(KiottyNetCode code);
        ConnectionAccess access();

        ReusablePool<Connection, CONNECTIONS> _connections;
        ConnectionHost                        _host;
        NotificationDesk                      _desk;
        EventListener                         _listener;
        Acceptor                              _acceptor;
        TlsProfile                            _profile;
        IoLoop                                _loop;
        RunGate                               _gate;
        std::atomic<bool>                     _is_closing;
        std::atomic<bool>                     _is_abandoned;
    };
}
