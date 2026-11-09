#include "context/kiotty_stream_client.h"

#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "data/infra/socket/kiotty_connect_with_retry.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetResult unconnected();
    }

    StreamClient::StreamClient()
        : _receive()
        , _send()
        , _tls()
        , _profile()
        , _socket()
        , _configuration()
        , _is_stopping(false)
    {
    }

    StreamClient::~StreamClient() = default;

    KiottyNetCode StreamClient::initialize(PoolHandle id, ClientConfiguration configuration)
    {
        (void)id;
        const KiottyNetClientConfiguration& client = configuration.client;
        if (!isInitialized(client)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode secured = openProfile(configuration.tls);
        if (secured != KIOTTY_NET_CODE_SUCCESS) { return secured; }
        _receive.reserve(client.receive_capacity);
        _send.reserve(client.send_capacity);
        if (configuration.tls != nullptr)
        {
            const TlsCapacity cipher{ client.receive_capacity, client.send_capacity };
            reserveTlsStages(_tls.stages(), cipher);
        }
        _receive.limit(client.receive_capacity);
        _send.limit(client.send_capacity);
        _configuration = client;
        _is_stopping.store(false);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void StreamClient::reset() noexcept
    {
        _is_stopping.store(true);
        disconnect();
        _profile.close();
    }

    KiottyNetCode StreamClient::connect()
    {
        disconnect();
        ConnectedResult connected = connectWithRetry(_configuration, _is_stopping);
        if (!connected) { return connected.code; }
        _socket = std::move(connected.value);
        const KiottyNetCode secured = secure();
        if (secured == KIOTTY_NET_CODE_SUCCESS) { return KIOTTY_NET_CODE_SUCCESS; }
        disconnect();
        return secured;
    }

    void StreamClient::disconnect()
    {
        _tls.close();
        _socket.close();
        _receive.clear();
        _send.clear();
    }

    KiottyNetResult StreamClient::read(Span<char> buffer, uint32_t timeout_milliseconds)
    {
        if (!_socket.valid()) { return unconnected(); }
        return stream().read(buffer, timeout_milliseconds);
    }

    KiottyNetResult StreamClient::write(KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        if (!_socket.valid()) { return unconnected(); }
        return stream().write(data, timeout_milliseconds);
    }

    KiottyNetCode StreamClient::notify(int64_t id, KiottyNetSpan data)
    {
        (void)id;
        (void)data;
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    KiottyNetConnectionInfo StreamClient::info(int64_t id) const
    {
        (void)id;
        return KiottyNetConnectionInfo{};
    }

    KiottyNetResult StreamClient::readStream(int64_t id,
                                             Span<char> buffer,
                                             uint32_t timeout_milliseconds)
    {
        (void)id;
        (void)buffer;
        (void)timeout_milliseconds;
        return KiottyNetResult{ 0, KIOTTY_NET_CODE_INVALID_ARGUMENT };
    }

    KiottyNetResult StreamClient::writeStream(int64_t id,
                                              KiottyNetSpan data,
                                              uint32_t timeout_milliseconds)
    {
        (void)id;
        (void)data;
        (void)timeout_milliseconds;
        return KiottyNetResult{ 0, KIOTTY_NET_CODE_INVALID_ARGUMENT };
    }

    KiottyNetCode StreamClient::writeStreamFile(int64_t id,
                                                const char* path,
                                                FileRange range,
                                                uint32_t timeout_milliseconds)
    {
        (void)id;
        (void)path;
        (void)range;
        (void)timeout_milliseconds;
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    bool StreamClient::isStreamOpen(int64_t id)
    {
        (void)id;
        return false;
    }

    KiottyNetCode StreamClient::closeStream(int64_t id, const KiottyNetConnectionAction& then)
    {
        (void)id;
        (void)then;
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    KiottyNetCode StreamClient::openProfile(const KiottyNetTlsClientConfiguration* tls)
    {
        if (tls == nullptr) { return KIOTTY_NET_CODE_SUCCESS; }
        if (!isInitialized(*tls)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return _profile.openClient(*tls);
    }

    KiottyNetCode StreamClient::secure()
    {
        if (!_profile.valid()) { return KIOTTY_NET_CODE_SUCCESS; }
        const TlsCapacity capacity{ _configuration.receive_capacity,
                                    _configuration.send_capacity };
        const KiottyNetCode opened = _tls.open(_profile, hostnameOf(_configuration), capacity);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        return stream().handshake(_configuration.connect_timeout_milliseconds);
    }

    ConnectionStream StreamClient::stream()
    {
        return ConnectionStream(_socket, _tls, _receive, _send);
    }

    namespace
    {
        KiottyNetResult unconnected()
        {
            return KiottyNetResult{ 0, KIOTTY_NET_CODE_CLOSED };
        }
    }
}
