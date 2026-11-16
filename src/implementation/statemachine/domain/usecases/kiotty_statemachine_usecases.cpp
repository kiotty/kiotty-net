#include "statemachine/domain/usecases/kiotty_statemachine_usecases.h"

#include "statemachine/data/repository/kiotty_stateful_handler.h"

namespace kiotty::net::implementation
{
    KiottyNetCode runStatefulServer(Server& server,
                                    const KiottyNetAddress& address,
                                    const KiottyNetTlsServerConfiguration* tls,
                                    const KiottyNetStatefulAppContext& stateful)
    {
        const StatefulHandler handler(stateful, server);
        return server.run(handler, address, tls);
    }

    KiottyNetCode runStatefulEventClient(EventClient& client,
                                         const KiottyNetAddress& address,
                                         const KiottyNetTlsClientConfiguration* tls,
                                         const KiottyNetStatefulAppContext& stateful)
    {
        const StatefulHandler handler(stateful, client);
        return client.run(handler, address, tls);
    }
}
