#include "base/domain/usecases/kiotty_client_usecases.h"

#include "base/data/datasource/kiotty_memory_layout.h"
#include "base/data/repository/configuration/kiotty_configuration.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    size_t clientMemorySize(const KiottyNetClientConfiguration& configuration)
    {
        if (!isInitialized(configuration)) { return 0; }
        const size_t receive = configuration.receive_capacity;
        const size_t send = configuration.send_capacity;
        const size_t push = configuration.push_capacity;
        const size_t largest = std::max(sizeof(EventClient), sizeof(StreamClient));
        return largest + stageBytesOf(receive, send, push);
    }

    KiottyNetResult readStreamClient(StreamClient& client,
                                     Span<char> buffer,
                                     uint32_t timeout_milliseconds)
    {
        const StreamResult stream = client.stream();
        if (!stream) { return KiottyNetResult{ 0, stream.code }; }
        return stream.value().read(buffer, timeout_milliseconds);
    }

    KiottyNetResult writeStreamClient(StreamClient& client,
                                      KiottyNetSpan data,
                                      uint32_t timeout_milliseconds)
    {
        const StreamResult stream = client.stream();
        if (!stream) { return KiottyNetResult{ 0, stream.code }; }
        return stream.value().write(data, timeout_milliseconds);
    }

    KiottyNetCode notifyEventClient(EventClient& client, KiottyNetSpan data)
    {
        const KiottyNetConnectionHandle connection = client.connection();
        return client.notify(connection.id, data);
    }
}
