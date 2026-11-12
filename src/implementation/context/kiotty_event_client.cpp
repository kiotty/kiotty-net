#include "context/kiotty_event_client.h"

#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "data/infra/socket/kiotty_connect_with_retry.h"
#include "domain/entity/kiotty_handler.h"
#include "domain/entity/kiotty_socket_code.h"
#include "domain/usecases/io_loop/kiotty_loop_parts.h"
#include "domain/usecases/io_loop/kiotty_stop_policy.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        ConnectionLimits limitsOf(const KiottyNetClientConfiguration& configuration);
        TlsCapacity      cipherOf(ConnectionLimits limits);
    }

    EventClient::EventClient()
        : _connections()
        , _host(_connections)
        , _desk(_connections, _host.notificationMutex())
        , _listener()
        , _acceptor()
        , _profile()
        , _loop()
        , _gate()
        , _configuration()
        , _is_closing(false)
        , _is_abandoned(false)
    {
    }

    EventClient::~EventClient() = default;

    KiottyNetCode EventClient::initialize(PoolHandle id, const KiottyNetClientConfiguration& client)
    {
        (void)id;
        if (!isInitialized(client)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const SocketCode opened = _listener.open(LISTENER_CAPACITY);
        if (opened != SocketCode::SUCCESS) { return toNetCode(opened); }
        const ConnectionLimits limits = limitsOf(client);
        _host.initialize(limits, CONNECTIONS, cipherOf(limits));
        _configuration = client;
        _gate.clear();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void EventClient::reset() noexcept
    {
        close();
        _gate.join();
        _loop.reset();
        _is_closing.store(false);
        _listener.close();
        _profile.close();
        _gate.clear();
        _is_abandoned.store(false);
    }

    KiottyNetCode EventClient::run(const Handler& handler,
                                   const KiottyNetAddress& address,
                                   const KiottyNetTlsClientConfiguration* tls)
    {
        const KiottyNetCode checked = handler.validate();
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        if (!isAddressValid(address)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (tls != nullptr && !isInitialized(*tls)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool entered = _gate.enter();
        if (!entered) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _is_closing.store(false);
        _loop.reset();
        const KiottyNetCode secured = openProfile(tls);
        if (secured != KIOTTY_NET_CODE_SUCCESS) { return leave(secured); }
        ConnectedResult connected = connectWithRetry(_configuration, address, _is_closing);
        if (!connected) { return leave(connected.code); }
        const LoopParts parts{ _host, _listener, _acceptor, _profile, handler };
        _gate.start();
        _loop.admit(parts, std::move(connected.value), hostnameOf(address));
        (void)_loop.run(parts, StopPolicy::WHEN_EMPTY);
        const bool is_closed = _is_closing.load();
        return leave(is_closed ? KIOTTY_NET_CODE_CLOSED : KIOTTY_NET_CODE_PEER_CLOSED);
    }

    void EventClient::close()
    {
        _is_closing.store(true);
        _loop.stop();
        (void)_listener.wake();
    }

    KiottyNetCode EventClient::await(uint64_t timeout_milliseconds)
    {
        return _gate.await(timeout_milliseconds);
    }

    KiottyNetConnectionHandle EventClient::connection()
    {
        const PoolHandle ready = _desk.findReady();
        return KiottyNetConnectionHandle{ this, ready.value() };
    }

    RunLifetime EventClient::lifetime()
    {
        return RunLifetime(_gate, _is_abandoned);
    }

    KiottyNetCode EventClient::notify(int64_t id, KiottyNetSpan data)
    {
        return access().notify(id, data);
    }

    KiottyNetCode EventClient::notify(int64_t id, Span<const KiottyNetSpan> pieces)
    {
        return access().notify(id, pieces);
    }

    KiottyNetConnectionInfo EventClient::info(int64_t id) const
    {
        return _desk.info(PoolHandle::fromValue(id));
    }

    KiottyNetResult EventClient::readStream(int64_t id,
                                            Span<char> buffer,
                                            uint32_t timeout_milliseconds)
    {
        return access().readStream(id, buffer, timeout_milliseconds);
    }

    KiottyNetResult EventClient::writeStream(int64_t id,
                                             KiottyNetSpan data,
                                             uint32_t timeout_milliseconds)
    {
        return access().writeStream(id, data, timeout_milliseconds);
    }

    KiottyNetCode EventClient::writeStreamFile(int64_t id,
                                               Span<const char> path,
                                               FileRange range,
                                               uint32_t timeout_milliseconds)
    {
        return access().writeStreamFile(id, path, range, timeout_milliseconds);
    }

    bool EventClient::isStreamOpen(int64_t id)
    {
        return access().isStreamOpen(id);
    }

    KiottyNetCode EventClient::closeStream(int64_t id, const KiottyNetConnectionAction& then)
    {
        return access().closeStream(id, then);
    }

    KiottyNetCode EventClient::openProfile(const KiottyNetTlsClientConfiguration* tls)
    {
        _profile.close();
        if (tls == nullptr) { return KIOTTY_NET_CODE_SUCCESS; }
        return _profile.openClient(*tls);
    }

    KiottyNetCode EventClient::leave(KiottyNetCode code)
    {
        _gate.finish(code);
        return code;
    }

    ConnectionAccess EventClient::access()
    {
        return ConnectionAccess(_desk, _listener, _loop);
    }

    namespace
    {
        ConnectionLimits limitsOf(const KiottyNetClientConfiguration& configuration)
        {
            return ConnectionLimits{ configuration.receive_capacity,
                                     configuration.send_capacity,
                                     configuration.push_capacity,
                                     configuration.send_timeout_milliseconds };
        }

        TlsCapacity cipherOf(ConnectionLimits limits)
        {
            if (!MemoryLimits::TLS) { return TlsCapacity{ 0, 0 }; }
            return TlsCapacity{ limits.receive, gatheredPlainBytes(limits.send, limits.notify) };
        }
    }
}
