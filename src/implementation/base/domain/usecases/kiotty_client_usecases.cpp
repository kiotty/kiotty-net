#include "base/domain/usecases/kiotty_client_usecases.h"

#include "base/data/datasource/kiotty_memory_layout.h"
#include "base/data/repository/configuration/kiotty_configuration.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetCode cancelledOr(StreamClient& client, KiottyNetCode code);
    }

    size_t clientMemorySize(const KiottyNetClientConfiguration& configuration)
    {
        if (!isInitialized(configuration)) { return 0; }
        const size_t receive = configuration.receive_capacity;
        const size_t send = configuration.send_capacity;
        const size_t push = configuration.push_capacity;
        const size_t largest = std::max(sizeof(EventClient), sizeof(StreamClient));
        return largest + stageBytesOf(receive, send, push);
    }

    KiottyNetCode connectStreamClient(StreamClient& client,
                                      const KiottyNetAddress& address,
                                      const KiottyNetTlsClientConfiguration* tls)
    {
        const bool is_cancelled = client.stop().accept();
        if (is_cancelled) { return KIOTTY_NET_CODE_CANCELLED; }
        const KiottyNetCode connected = client.connect(address, tls);
        return cancelledOr(client, connected);
    }

    void disconnectStreamClient(StreamClient& client)
    {
        client.disconnect();
        static_cast<void>(client.stop().accept());
    }

    KiottyNetCode cancelStreamClient(StreamClient& client)
    {
        client.stop().raise();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult readStreamClient(StreamClient& client,
                                     Span<char> buffer,
                                     uint32_t timeout_milliseconds)
    {
        const bool is_cancelled = client.stop().accept();
        if (is_cancelled) { return KiottyNetResult{ 0, KIOTTY_NET_CODE_CANCELLED }; }
        const StreamResult stream = client.stream();
        if (!stream) { return KiottyNetResult{ 0, stream.code }; }
        const KiottyNetResult got = stream.value().read(buffer, timeout_milliseconds);
        return KiottyNetResult{ got.bytes, cancelledOr(client, got.code) };
    }

    KiottyNetResult writeStreamClient(StreamClient& client,
                                      KiottyNetSpan data,
                                      uint32_t timeout_milliseconds)
    {
        const bool is_cancelled = client.stop().accept();
        if (is_cancelled) { return KiottyNetResult{ 0, KIOTTY_NET_CODE_CANCELLED }; }
        const StreamResult stream = client.stream();
        if (!stream) { return KiottyNetResult{ 0, stream.code }; }
        const KiottyNetResult written = stream.value().write(data, timeout_milliseconds);
        return KiottyNetResult{ written.bytes, cancelledOr(client, written.code) };
    }

    KiottyNetCode notifyEventClient(EventClient& client, KiottyNetSpan data)
    {
        const KiottyNetConnectionHandle connection = client.connection();
        return client.notify(connection.id, data);
    }

    namespace
    {
        KiottyNetCode cancelledOr(StreamClient& client, KiottyNetCode code)
        {
            if (code != KIOTTY_NET_CODE_CLOSED) { return code; }
            const bool is_cancelled = client.stop().accept();
            return is_cancelled ? KIOTTY_NET_CODE_CANCELLED : code;
        }
    }
}
