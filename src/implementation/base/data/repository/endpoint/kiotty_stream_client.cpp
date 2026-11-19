#include "base/data/repository/endpoint/kiotty_stream_client.h"

#include "base/data/infra/socket/kiotty_connect_with_retry.h"
#include "base/data/repository/configuration/kiotty_configuration.h"
#include "base/data/repository/configuration/kiotty_tls_configuration.h"

#include <utility>

namespace kiotty::net::implementation
{
    StreamClient::StreamClient()
        : _receive()
        , _send()
        , _tls()
        , _profile()
        , _socket()
        , _configuration()
        , _stop()
        , _stream(_socket, _tls, _receive, _send)
    {
    }

    StreamClient::~StreamClient() = default;

    KiottyNetCode StreamClient::initialize(PoolHandle id,
                                           const KiottyNetClientConfiguration& client)
    {
        (void)id;
        if (!isInitialized(client)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const SocketCode opened = _stop.open();
        if (opened != SocketCode::SUCCESS) { return toNetCode(opened); }
        static_cast<void>(_stop.accept());
        _receive.reserve(client.receive_capacity);
        _send.reserve(client.send_capacity);
        const TlsCapacity cipher{ client.receive_capacity, client.send_capacity };
        reserveTlsStages(_tls.stages(), cipher);
        _receive.limit(client.receive_capacity);
        _send.limit(client.send_capacity);
        _configuration = client;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void StreamClient::reset() noexcept
    {
        _stop.raise();
        disconnect();
        _profile.close();
    }

    KiottyNetCode StreamClient::connect(const KiottyNetAddress& address,
                                        const KiottyNetTlsClientConfiguration* tls)
    {
        const KiottyNetCode prepared = prepare(address, tls);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        return reconnect(address, Deadline(0));
    }

    KiottyNetCode StreamClient::reconnect(const KiottyNetAddress& address, const Deadline& deadline)
    {
        disconnect();
        ConnectedResult connected = connectWithRetry(address, _stop, deadline);
        if (!connected) { return connected.code; }
        _socket = std::move(connected.value);
        const KiottyNetCode secured = secure(address, deadline);
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

    StreamResult StreamClient::stream()
    {
        if (!_socket.valid()) { return StreamResult::failure(KIOTTY_NET_CODE_CLOSED); }
        return StreamResult::success(_stream);
    }

    StopSignal& StreamClient::stop()
    {
        return _stop;
    }

    KiottyNetCode StreamClient::notify(int64_t id, KiottyNetSpan data)
    {
        (void)id;
        (void)data;
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    KiottyNetCode StreamClient::notify(int64_t id, Span<const KiottyNetSpan> pieces)
    {
        (void)id;
        (void)pieces;
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
                                                Span<const char> path,
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

    KiottyNetCode StreamClient::prepare(const KiottyNetAddress& address,
                                        const KiottyNetTlsClientConfiguration* tls)
    {
        disconnect();
        if (!isAddressValid(address)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (tls != nullptr && !isInitialized(*tls)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return openProfile(tls);
    }

    KiottyNetCode StreamClient::openProfile(const KiottyNetTlsClientConfiguration* tls)
    {
        _profile.close();
        if (tls == nullptr) { return KIOTTY_NET_CODE_SUCCESS; }
        return _profile.openClient(*tls);
    }

    KiottyNetCode StreamClient::secure(const KiottyNetAddress& address, const Deadline& deadline)
    {
        if (!_profile.valid()) { return KIOTTY_NET_CODE_SUCCESS; }
        const TlsCapacity capacity{ _configuration.receive_capacity,
                                    _configuration.send_capacity };
        const KiottyNetCode opened = _tls.open(_profile, hostnameOf(address), capacity);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        return _stream.handshake(deadline.bound(address.connect_timeout_milliseconds));
    }
}
