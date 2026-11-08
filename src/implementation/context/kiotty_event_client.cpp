#include "context/kiotty_event_client.h"

#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
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
    {
    }

    EventClient::~EventClient() = default;

    KiottyNetCode EventClient::initialize(PoolHandle id, ClientConfiguration configuration)
    {
        (void)id;
        const KiottyNetClientConfiguration& client = configuration.client;
        if (!isInitialized(client)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode secured = openProfile(configuration.tls);
        if (secured != KIOTTY_NET_CODE_SUCCESS) { return secured; }
        const SocketCode opened = _listener.open(LISTENER_CAPACITY);
        if (opened != SocketCode::SUCCESS) { return toNetCode(opened); }
        _host.initialize(limitsOf(client), CONNECTIONS);
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
    }

    KiottyNetCode EventClient::run(const KiottyNetConnectionHandler& callbacks,
                                   KiottyNetAppContext app_context)
    {
        const bool entered = _gate.enter();
        if (!entered) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _is_closing.store(false);
        _loop.reset();
        ConnectedResult connected = connectWithRetry(_configuration, _is_closing);
        if (!connected) { return leave(connected.code); }
        const Handler handler(callbacks, app_context, *this);
        const LoopParts parts{ _host, _listener, _acceptor, _profile, handler };
        _gate.start();
        _loop.admit(parts, std::move(connected.value), hostnameOf(_configuration));
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

    KiottyNetCode EventClient::notify(KiottyNetSpan data)
    {
        const PoolHandle ready = _desk.findReady();
        return access().notify(ready.value(), data);
    }

    KiottyNetCode EventClient::notify(int64_t id, KiottyNetSpan data)
    {
        return access().notify(id, data);
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
                                               const char* path,
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
        if (tls == nullptr) { return KIOTTY_NET_CODE_SUCCESS; }
        if (!isInitialized(*tls)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
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
                                     configuration.push_capacity };
        }
    }
}
