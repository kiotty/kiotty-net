#include "base/domain/usecases/kiotty_server_usecases.h"

#include "base/data/datasource/kiotty_memory_layout.h"
#include "base/data/repository/configuration/kiotty_configuration.h"
#include "base/data/repository/connection/kiotty_connection.h"
#include "base/data/repository/endpoint/kiotty_server.h"

namespace kiotty::net::implementation
{
    size_t serverMemorySize(const KiottyNetServerConfiguration& configuration)
    {
        if (!isInitialized(configuration)) { return 0; }
        const size_t receive = configuration.receive_capacity;
        const size_t send = configuration.send_capacity;
        const size_t push = configuration.push_capacity;
        const size_t connection = sizeof(Connection) + stageBytesOf(receive, send, push);
        return sizeof(Server) + configuration.max_clients * connection;
    }
}
