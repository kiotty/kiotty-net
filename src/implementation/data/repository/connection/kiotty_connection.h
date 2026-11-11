#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_stage_set.h"
#include "core/pool/kiotty_pool_handle.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "data/repository/connection/kiotty_caller.h"
#include "data/repository/connection/kiotty_connection_configuration.h"
#include "data/repository/connection/kiotty_connection_flows.h"
#include "data/repository/connection/kiotty_connection_parts.h"
#include "data/repository/connection/kiotty_connection_state.h"
#include "data/repository/connection/kiotty_connection_status.h"
#include "data/repository/connection/kiotty_io_source.h"
#include "data/repository/connection/kiotty_work_context.h"
#include "domain/entity/kiotty_handler.h"
#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_phase.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    class Connection : public IoSource
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = ConnectionConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode OUT_OF_MEMORY = KIOTTY_NET_CODE_OUT_OF_MEMORY;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_PEER_CLOSED;

        Connection();
        ~Connection() override;

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&&) = delete;
        Connection& operator=(Connection&&) = delete;

        KiottyNetCode   initialize(PoolHandle id, ConnectionConfiguration&& configuration);
        void            reset() noexcept;
        void            reserve(ConnectionLimits limits, TlsCapacity cipher);
        IoAction        onOpened(const Handler& handler);
        IoAction        dispatch(const IoCompletion& completion,
                                 const Handler& handler,
                                 std::mutex& notification);
        IoAction        close(KiottyNetCode reason, const Handler& handler);
        ConnectionState  state() const;
        ConnectionStatus status() const;
        ConnectionFlows  flows();

        SocketHandle socket() const override;
        int32_t      slot() const override;
        IoEvent&     eventFor(const IoAction& action) override;
        IoPayload    resolve(const IoAction& action) override;
        SocketCode   attach(EventListener& listener) override;
        void         detach(EventListener& listener) override;
        void         shutdown() override;

    private:
        KiottyNetCode   open(TlsProfile& profile, Span<const char> hostname, TlsCapacity capacity);
        IoAction        react(const IoCompletion& completion,
                              const Handler& handler,
                              std::mutex& notification);
        IoAction        onReceived(const IoCompletion& completion, const Handler& handler);
        IoAction        onSent(const IoCompletion& completion,
                               const Handler& handler,
                               std::mutex& notification);
        IoAction        onWritten(size_t length, const Handler& handler);
        IoAction        onTimeout(const Handler& handler);
        IoAction        onWorkReady(const Handler& handler);
        IoAction        onHandshake(const Handler& handler);
        IoAction        onReady(const Handler& handler);
        IoAction        extend(const KiottyNetConnectionAction& returned, const Caller& caller);
        IoAction        apply(IoAction action, const Handler& handler);
        void            enterClosing(KiottyNetCode reason, const Handler& handler);
        bool            isUserActionPending();
        void            setDeadline(uint32_t timeout_milliseconds);
        Caller          callerFor(const Handler& handler);
        ConnectionParts parts();

        StageSet                   _plain;
        TlsEngine                  _tls;
        ActiveSocket               _socket;
        KiottyNetConnectionInfo    _info;
        IoEvent                    _read_event;
        IoEvent                    _write_event;
        WorkContext                _work;
        WorkFlow                   _work_flow;
        SendFlow                   _send_flow;
        ReceiveFlow                _receive_flow;
        std::atomic<uint64_t>      _deadline;
        KiottyNetConnectionContext _context;
        PoolHandle                 _id;
        int32_t                    _machine_state;
        std::atomic<Phase>         _phase;
    };

    inline ConnectionStatus Connection::status() const
    {
        ConnectionStatus snapshot{};
        snapshot.deadline = _deadline.load();
        snapshot.send_deadline = _send_flow.deadline();
        snapshot.phase = _phase.load();
        snapshot.pending = _work_flow.pending();
        return snapshot;
    }

    using ConnectionResult = Result<Connection&, KiottyNetCode>;
}
