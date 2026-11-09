#include "context/kiotty_server.h"

#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "domain/entity/kiotty_handler.h"
#include "domain/entity/kiotty_socket_code.h"
#include "domain/usecases/io_loop/kiotty_loop_parts.h"
#include "domain/usecases/io_loop/kiotty_stop_policy.h"

namespace kiotty::net::implementation
{
    namespace
    {
        ConnectionLimits limitsOf(const KiottyNetServerConfiguration& configuration);
        size_t           listenerCapacityOf(size_t max_clients);
        TlsCapacity      cipherOf(ConnectionLimits limits,
                                  const KiottyNetTlsServerConfiguration* tls);
    }

    Server::Server()
        : _connections()
        , _host(_connections)
        , _desk(_connections, _host.notificationMutex())
        , _listener()
        , _acceptor()
        , _profile()
        , _loop()
        , _gate()
        , _is_abandoned(false)
        , _configuration()
    {
    }

    Server::~Server() = default;

    KiottyNetCode Server::initialize(PoolHandle id, ServerConfiguration configuration)
    {
        (void)id;
        const KiottyNetServerConfiguration& server = configuration.server;
        if (!isInitialized(server)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode secured = openProfile(configuration.tls);
        if (secured != KIOTTY_NET_CODE_SUCCESS) { return secured; }
        const bool is_sized = _connections.resize(server.max_clients);
        if (!is_sized) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const SocketCode opened = _listener.open(listenerCapacityOf(server.max_clients));
        if (opened != SocketCode::SUCCESS) { return toNetCode(opened); }
        const ConnectionLimits limits = limitsOf(server);
        _host.initialize(limits, server.max_clients, cipherOf(limits, configuration.tls));
        _configuration = server;
        _gate.clear();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void Server::reset() noexcept
    {
        close();
        _gate.join();
        _loop.reset();
        _listener.close();
        _profile.close();
        _gate.clear();
        _is_abandoned.store(false);
    }

    KiottyNetCode Server::run(const KiottyNetConnectionHandler& callbacks,
                              KiottyNetAppContext app_context)
    {
        const bool entered = _gate.enter();
        if (!entered) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _loop.reset();
        const Handler handler(callbacks, app_context, *this);
        const LoopParts parts{ _host, _listener, _acceptor, _profile, handler };
        const KiottyNetCode opened = listen();
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return leave(opened); }
        _gate.start();
        (void)_loop.run(parts, StopPolicy::WHEN_STOPPED);
        return leave(KIOTTY_NET_CODE_CLOSED);
    }

    void Server::close()
    {
        _loop.stop();
        (void)_listener.wake();
    }

    KiottyNetCode Server::await(uint64_t timeout_milliseconds)
    {
        return _gate.await(timeout_milliseconds);
    }

    KiottyNetCode Server::broadcast(KiottyNetSpan data)
    {
        const KiottyNetCode staged = _desk.broadcast(data);
        if (staged != KIOTTY_NET_CODE_SUCCESS) { return staged; }
        (void)_listener.wake();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    RunLifetime Server::lifetime()
    {
        return RunLifetime(_gate, _is_abandoned);
    }

    KiottyNetCode Server::notify(int64_t id, KiottyNetSpan data)
    {
        return access().notify(id, data);
    }

    KiottyNetConnectionInfo Server::info(int64_t id) const
    {
        return _desk.info(PoolHandle::fromValue(id));
    }

    KiottyNetResult Server::readStream(int64_t id,
                                       Span<char> buffer,
                                       uint32_t timeout_milliseconds)
    {
        return access().readStream(id, buffer, timeout_milliseconds);
    }

    KiottyNetResult Server::writeStream(int64_t id,
                                        KiottyNetSpan data,
                                        uint32_t timeout_milliseconds)
    {
        return access().writeStream(id, data, timeout_milliseconds);
    }

    KiottyNetCode Server::writeStreamFile(int64_t id,
                                          const char* path,
                                          FileRange range,
                                          uint32_t timeout_milliseconds)
    {
        return access().writeStreamFile(id, path, range, timeout_milliseconds);
    }

    bool Server::isStreamOpen(int64_t id)
    {
        return access().isStreamOpen(id);
    }

    KiottyNetCode Server::closeStream(int64_t id, const KiottyNetConnectionAction& then)
    {
        return access().closeStream(id, then);
    }

    KiottyNetCode Server::openProfile(const KiottyNetTlsServerConfiguration* tls)
    {
        if (tls == nullptr) { return KIOTTY_NET_CODE_SUCCESS; }
        if (!isInitialized(*tls)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return _profile.openServer(*tls);
    }

    KiottyNetCode Server::listen()
    {
        const NoDelay no_delay = noDelayOf(_configuration.no_delay);
        const SocketCode opened = _acceptor.open(_configuration.address, LISTEN_BACKLOG, no_delay);
        if (opened != SocketCode::SUCCESS) { return toNetCode(opened); }
        const SocketCode attached = _acceptor.attach(_listener);
        if (attached == SocketCode::SUCCESS) { return KIOTTY_NET_CODE_SUCCESS; }
        _acceptor.close();
        return toNetCode(attached);
    }

    KiottyNetCode Server::leave(KiottyNetCode code)
    {
        _gate.finish(code);
        return code;
    }

    ConnectionAccess Server::access()
    {
        return ConnectionAccess(_desk, _listener, _loop);
    }

    namespace
    {
        ConnectionLimits limitsOf(const KiottyNetServerConfiguration& configuration)
        {
            return ConnectionLimits{ configuration.receive_capacity,
                                     configuration.send_capacity,
                                     configuration.push_capacity };
        }

        size_t listenerCapacityOf(size_t max_clients)
        {
            return 2 * max_clients + 2;
        }

        TlsCapacity cipherOf(ConnectionLimits limits, const KiottyNetTlsServerConfiguration* tls)
        {
            if (tls == nullptr) { return TlsCapacity{ 0, 0 }; }
            return TlsCapacity{ limits.receive, largerOf(limits.send, limits.notify) };
        }
    }
}
