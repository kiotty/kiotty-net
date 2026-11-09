#pragma once

#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "data/repository/connection/kiotty_connection_configuration.h"
#include "data/repository/connection/kiotty_io_source.h"
#include "domain/entity/kiotty_io_action.h"
#include "domain/entity/kiotty_io_completion.h"
#include "domain/usecases/io_loop/kiotty_loop_parts.h"
#include "domain/usecases/io_loop/kiotty_stop_policy.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace kiotty::net::implementation
{
    class Connection;

    class IoLoop
    {
    public:
        IoLoop();
        IoLoop(const IoLoop&) = delete;
        IoLoop& operator=(const IoLoop&) = delete;

        KiottyNetCode run(const LoopParts& parts, StopPolicy policy);
        void          stop();
        void          reset();
        bool          isIoThread() const;
        void          admit(const LoopParts& parts, ActiveSocket&& socket,
                            Span<const char> hostname);

    private:
        static constexpr uint64_t ACCEPT_RETRY_MILLISECONDS = 1000;

        void runOnce(const LoopParts& parts, StopPolicy policy);
        void drain(const LoopParts& parts);
        void settle(const LoopParts& parts, StopPolicy policy);
        void releaseSettled(const LoopParts& parts);
        void rearmAccept(const LoopParts& parts);
        void armAccept(const LoopParts& parts);
        Span<const IoCompletion> collectIoEvents(const LoopParts& parts);
        uint32_t waitTimeout(const LoopParts& parts, uint64_t now);
        void     shutdown(const LoopParts& parts);

        void stepUntilQuiet(const LoopParts& parts);
        bool stepLocalEvents(const LoopParts& parts);
        bool stepSlot(const LoopParts& parts, int32_t slot, uint64_t now);
        void step(const IoCompletion& completion, const LoopParts& parts);
        void access(const IoCompletion& completion, const LoopParts& parts);
        void welcome(ActiveSocket&& socket, Span<const char> hostname, const LoopParts& parts);
        void open(ConnectionConfiguration&& configuration, const LoopParts& parts);
        void submitTo(Connection& connection, const IoAction& action, const LoopParts& parts);
        void reportAcceptFailure(KiottyNetCode code, const LoopParts& parts);

        SocketCode submit(IoSource& source, const IoAction& action, EventListener& listener);

        std::atomic<bool>            _stopped;
        std::atomic<std::thread::id> _io_thread;
        uint64_t                     _accept_retry_at;
    };
}
