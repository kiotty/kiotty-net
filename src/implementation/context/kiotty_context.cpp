#include "context/kiotty_context.h"

#include "data/infra/socket/kiotty_socket_startup.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "domain/entity/kiotty_socket_code.h"

#include <cassert>

namespace kiotty::net::implementation
{
    namespace
    {
        template <class T>
        void sweep(LazyReusablePool<T>& pool)
        {
            pool.forEachActive([&pool](T& owner) {
                if (owner.lifetime().isReleasable()) { pool.release(owner); }
            });
        }

        template <class T>
        void closeAll(LazyReusablePool<T>& pool)
        {
            pool.forEachActive([](T& owner) { owner.close(); });
        }

        template <class T>
        void joinAll(LazyReusablePool<T>& pool)
        {
            pool.forEachActive([](T& owner) {
                RunLifetime life = owner.lifetime();
                assert(!life.isRunner());
                if (!life.isRunner()) { life.join(); }
            });
        }
    }

    ContextResult Context::open()
    {
        static KiottyNetContextImpl instance;
        Context& context = instance;
        const std::lock_guard<std::mutex> lock(context._mutex);
        const KiottyNetCode initialized = context.initialize();
        if (initialized != KIOTTY_NET_CODE_SUCCESS) { return ContextResult::failure(initialized); }
        return ContextResult::success(instance);
    }

    void Context::release()
    {
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            if (_phase != ContextPhase::OPEN) { return; }
            _phase = ContextPhase::RELEASING;
        }
        closeAll(_servers);
        closeAll(_event_clients);
        joinAll(_servers);
        joinAll(_event_clients);
        finishRelease();
    }

    ServerResult Context::createServer(const KiottyNetServerConfiguration& configuration)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_phase != ContextPhase::OPEN)
        {
            return ServerResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        sweep(_servers);
        return _servers.acquire(configuration);
    }

    StreamClientResult Context::createStreamClient(
        const KiottyNetClientConfiguration& configuration)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_phase != ContextPhase::OPEN)
        {
            return StreamClientResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        return _stream_clients.acquire(configuration);
    }

    EventClientResult Context::createEventClient(const KiottyNetClientConfiguration& configuration)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_phase != ContextPhase::OPEN)
        {
            return EventClientResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        sweep(_event_clients);
        return _event_clients.acquire(configuration);
    }

    void Context::destroy(KiottyNetServer& server)
    {
        retire(_servers, server);
    }

    void Context::destroy(KiottyNetStreamClient& client)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_phase != ContextPhase::OPEN) { return; }
        _stream_clients.release(client);
    }

    void Context::destroy(KiottyNetEventClient& client)
    {
        retire(_event_clients, client);
    }

    Context::Context()
        : _servers()
        , _stream_clients()
        , _event_clients()
        , _mutex()
        , _phase(ContextPhase::CLOSED)
    {
    }

    Context::~Context() = default;

    template <class T>
    void Context::retire(LazyReusablePool<T>& pool, T& owner)
    {
        RunLifetime life = owner.lifetime();
        owner.close();
        if (life.isRunner())
        {
            life.abandon();
            return;
        }
        life.join();
        const std::lock_guard<std::mutex> lock(_mutex);
        if (_phase != ContextPhase::OPEN) { return; }
        pool.release(owner);
    }

    KiottyNetCode Context::initialize()
    {
        if (_phase == ContextPhase::OPEN) { return KIOTTY_NET_CODE_SUCCESS; }
        if (_phase == ContextPhase::RELEASING) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const SocketCode started = startSockets();
        if (started != SocketCode::SUCCESS) { return toNetCode(started); }
        const KiottyNetCode secured = initializeTlsLibrary();
        if (secured != KIOTTY_NET_CODE_SUCCESS)
        {
            stopSockets();
            return secured;
        }
        _phase = ContextPhase::OPEN;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void Context::finishRelease()
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _servers.clear();
        _stream_clients.clear();
        _event_clients.clear();
        stopSockets();
        _phase = ContextPhase::CLOSED;
    }
}
