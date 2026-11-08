#include "context/kiotty_context.h"

#include "data/datasource/kiotty_memory_limits.h"
#include "data/infra/socket/kiotty_socket_startup.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "domain/entity/kiotty_socket_code.h"

namespace kiotty::net::implementation
{
    ContextResult Context::open()
    {
        static Context context;
        const std::lock_guard<std::mutex> lock(context._mutex);
        const KiottyNetCode initialized = context.initialize();
        if (initialized != KIOTTY_NET_CODE_SUCCESS) { return ContextResult::failure(initialized); }
        return ContextResult::success(context);
    }

    void Context::release()
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_initialized) { return; }
        _servers.clear();
        _stream_clients.clear();
        _event_clients.clear();
        stopSockets();
        _is_initialized = false;
    }

    ServerResult Context::createServer(const KiottyNetServerConfiguration& configuration,
                                       const KiottyNetTlsServerConfiguration* tls)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_initialized) { return ServerResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return _servers.acquire(ServerConfiguration{ configuration, tls });
    }

    StreamClientResult Context::createStreamClient(
        const KiottyNetClientConfiguration& configuration,
        const KiottyNetTlsClientConfiguration* tls)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_initialized)
        {
            return StreamClientResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        return _stream_clients.acquire(ClientConfiguration{ configuration, tls });
    }

    EventClientResult Context::createEventClient(const KiottyNetClientConfiguration& configuration,
                                                 const KiottyNetTlsClientConfiguration* tls)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_initialized)
        {
            return EventClientResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        return _event_clients.acquire(ClientConfiguration{ configuration, tls });
    }

    void Context::destroy(Server& server)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _servers.release(server);
    }

    void Context::destroy(StreamClient& client)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _stream_clients.release(client);
    }

    void Context::destroy(EventClient& client)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _event_clients.release(client);
    }

    Context::Context()
        : _servers()
        , _stream_clients()
        , _event_clients()
        , _mutex()
        , _is_initialized(false)
    {
    }

    Context::~Context() = default;

    KiottyNetCode Context::initialize()
    {
        if (_is_initialized) { return KIOTTY_NET_CODE_SUCCESS; }
        const SocketCode started = startSockets();
        if (started != SocketCode::SUCCESS) { return toNetCode(started); }
        const KiottyNetCode secured = initializeTlsLibrary();
        const KiottyNetCode reserved = reservePools();
        const KiottyNetCode failed = secured != KIOTTY_NET_CODE_SUCCESS ? secured : reserved;
        if (failed != KIOTTY_NET_CODE_SUCCESS)
        {
            stopSockets();
            return failed;
        }
        _is_initialized = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode Context::reservePools()
    {
        const bool is_reserved = _servers.capacity() > 0;
        if (is_reserved) { return KIOTTY_NET_CODE_SUCCESS; }
        const KiottyNetCode servers = _servers.initialize({ MemoryLimits::SERVERS });
        if (servers != KIOTTY_NET_CODE_SUCCESS) { return servers; }
        const KiottyNetCode streams = _stream_clients.initialize({ MemoryLimits::STREAM_CLIENTS });
        if (streams != KIOTTY_NET_CODE_SUCCESS) { return streams; }
        return _event_clients.initialize({ MemoryLimits::EVENT_CLIENTS });
    }
}
