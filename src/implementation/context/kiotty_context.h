#pragma once

#include "context/kiotty_handles.h"
#include "core/kiotty_result.h"
#include "core/pool/kiotty_lazy_reusable_pool.h"
#include "kiotty_net_base.h"

#include <cstdint>
#include <mutex>

struct KiottyNetContextImpl;

namespace kiotty::net::implementation
{

    enum class ContextPhase : uint8_t
    {
        CLOSED,
        OPEN,
        RELEASING
    };

    using ContextResult = Result<KiottyNetContextImpl&, KiottyNetCode>;
    using ServerResult = Result<KiottyNetServer&, KiottyNetCode>;
    using StreamClientResult = Result<KiottyNetStreamClient&, KiottyNetCode>;
    using EventClientResult = Result<KiottyNetEventClient&, KiottyNetCode>;

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
        void               destroy(KiottyNetServer& server);
        void               destroy(KiottyNetStreamClient& client);
        void               destroy(KiottyNetEventClient& client);

    protected:
        Context();
        ~Context();

    private:
        KiottyNetCode initialize();
        void          finishRelease();

        template <class T>
        void retire(LazyReusablePool<T>& pool, T& owner);

        LazyReusablePool<KiottyNetServer>       _servers;
        LazyReusablePool<KiottyNetStreamClient> _stream_clients;
        LazyReusablePool<KiottyNetEventClient>  _event_clients;
        std::mutex                              _mutex;
        ContextPhase                            _phase;
    };
}

struct KiottyNetContextImpl final : kiotty::net::implementation::Context
{
};
