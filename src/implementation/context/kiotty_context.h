#pragma once

#include "context/kiotty_event_client.h"
#include "context/kiotty_server.h"
#include "context/kiotty_stream_client.h"
#include "core/kiotty_result.h"
#include "core/pool/kiotty_lazy_reusable_pool.h"
#include "kiotty_net_base.h"

#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    class Context;

    enum class ContextPhase : uint8_t
    {
        CLOSED,
        OPEN,
        RELEASING
    };

    using ContextResult = Result<Context&, KiottyNetCode>;
    using ServerResult = Result<Server&, KiottyNetCode>;
    using StreamClientResult = Result<StreamClient&, KiottyNetCode>;
    using EventClientResult = Result<EventClient&, KiottyNetCode>;

    class Context
    {
    public:
        static ContextResult open();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        void               release();
        ServerResult       createServer(const KiottyNetServerConfiguration& configuration,
                                        const KiottyNetTlsServerConfiguration* tls);
        StreamClientResult createStreamClient(const KiottyNetClientConfiguration& configuration,
                                              const KiottyNetTlsClientConfiguration* tls);
        EventClientResult  createEventClient(const KiottyNetClientConfiguration& configuration,
                                             const KiottyNetTlsClientConfiguration* tls);
        void               destroy(Server& server);
        void               destroy(StreamClient& client);
        void               destroy(EventClient& client);

    private:
        Context();
        ~Context();

        KiottyNetCode initialize();
        KiottyNetCode reservePools();
        void          finishRelease();

        template <class Owner>
        void retire(LazyReusablePool<Owner>& pool, Owner& owner);

        LazyReusablePool<Server>       _servers;
        LazyReusablePool<StreamClient> _stream_clients;
        LazyReusablePool<EventClient>  _event_clients;
        std::mutex                     _mutex;
        ContextPhase                   _phase;
    };
}
