#if !defined(KIOTTY_CONNECTION_H)
#define KIOTTY_CONNECTION_H

#include "base/connection/kiotty_io_source.h"
#include "base/connection/kiotty_work_context.h"
#include "base/context/kiotty_owner.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "data/infra/socket/kiotty_passive_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"
#include "base/stream/kiotty_connection_stream.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "core/kiotty_stage_set.h"
#include "core/pool/kiotty_pool_handle.h"
#include "domain/entity/kiotty_connection_limits.h"
#include "domain/entity/kiotty_handler.h"
#include "domain/entity/kiotty_phase.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    struct ConnectionConfiguration
    {
        ActiveSocket               socket;
        KiottyNetConnectionContext context;
        const char*                hostname;
        ConnectionLimits           limits;
        TlsProfile&                profile;
    };

    typedef Result<IoAction, KiottyNetCode> AdoptResult;

    class Connection : public IoSource
    {
    public:
        typedef KiottyNetCode           code_type;
        typedef ConnectionConfiguration configuration_type;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;
        static constexpr KiottyNetCode OUT_OF_MEMORY = KIOTTY_NET_CODE_OUT_OF_MEMORY;
        static constexpr KiottyNetCode INVALID_HANDLE = KIOTTY_NET_CODE_PEER_CLOSED;

        Connection();
        ~Connection() override;

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&&) = delete;
        Connection& operator=(Connection&&) = delete;

        KiottyNetCode initialize(PoolHandle id, ConnectionConfiguration&& configuration);
        void          reset() noexcept;
        PoolHandle    id() const;
        int32_t       generation() const;
        bool          alive() const;
        Phase         phase() const;

        SocketHandle socket() const override;
        int32_t      slot() const override;
        IoEvent&     eventFor(const IoAction& action) override;
        IoPayload    resolve(const IoAction& action) override;
        SocketCode   attach(EventListener& listener) override;
        void         detach(EventListener& listener) override;
        void         shutdown() override;

        IoAction onOpened(const Handler& handler);
        IoAction close(KiottyNetCode reason, const Handler& handler);
        IoAction dispatch(const IoCompletion& completion, const Handler& handler, std::mutex& notification);

        IoAction pickWrite(std::mutex& notification);

        uint64_t       deadline() const;
        bool           expireDeadline(uint64_t now);
        bool           workReady() const;
        bool           workFinished() const;
        IoAction       takeWorkOutcome();
        bool           working() const;
        const IoEvent& readEvent() const;
        const IoEvent& writeEvent() const;

        KiottyNetConnectionInfo    info() const;
        KiottyNetConnectionContext context() const;
        int32_t                    state() const;
        void                       setState(int32_t state);
        size_t                     notificationWritable();
        KiottyNetCode              stageNotification(KiottyNetSpan data);

        KiottyNetCode    verifyWork(int32_t generation) const;
        ConnectionStream stream();
        KiottyNetCode    closeStream(int32_t generation, const KiottyNetConnectionAction& then);

    private:
        IoAction onReceived(const IoCompletion& completion, const Handler& handler);
        IoAction onSent(const IoCompletion& completion, const Handler& handler, std::mutex& notification);
        IoAction onTimeout(const Handler& handler);
        IoAction onWorkReady(const Handler& handler);
        IoAction onWorkFinished(const IoCompletion& completion, const Handler& handler);
        IoAction onHandshake(const Handler& handler);
        IoAction onPeerGone(SocketCode code);
        AdoptResult adopt(const KiottyNetConnectionAction& returned);
        IoAction admit(const KiottyNetConnectionAction& returned, const Handler& handler);
        IoAction drive(IoAction action, const Handler& handler);
        IoAction extend(const KiottyNetConnectionAction& returned, const Handler& handler);
        Result<size_t, KiottyNetCode> refill();
        IoAction apply(IoAction action, const Handler& handler);
        IoAction select(std::mutex& notification);
        IoAction encoded(IoAction write, Span<char> plain);
        bool     userActionPending();
        void     setDeadline(uint32_t timeout_milliseconds);
        void     reject(const Handler& handler, KiottyNetCode code, const char* message) const;
        StageBase& receiveStage(IoPurpose purpose);
        KiottyNetConnectionHandle handle(const Handler& handler) const;
        KiottyNetConnectionStream streamHandle(const Handler& handler) const;

        typedef StageSet<MemoryLimits::RECEIVE_CAPACITY, MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY> PlainStages;

        std::atomic<Phase>         _phase;
        uint64_t                   _deadline;
        WorkContext                _work;
        IoEvent                    _read_event;
        IoEvent                    _write_event;
        PlainStages                _plain;
        TlsEngine                  _tls;
        ActiveSocket               _socket;
        PoolHandle                 _id;
        KiottyNetConnectionContext _context;
        KiottyNetConnectionInfo    _info;
        std::atomic<int32_t>       _state;
    };
}

#endif
