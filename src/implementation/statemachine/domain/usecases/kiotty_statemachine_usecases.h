#pragma once

#include "base/data/repository/endpoint/kiotty_event_client.h"
#include "base/data/repository/endpoint/kiotty_server.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetCode runStatefulServer(Server& server,
                                    const KiottyNetAddress& address,
                                    const KiottyNetTlsServerConfiguration* tls,
                                    const KiottyNetStatefulAppContext& stateful);
    KiottyNetCode runStatefulEventClient(EventClient& client,
                                         const KiottyNetAddress& address,
                                         const KiottyNetTlsClientConfiguration* tls,
                                         const KiottyNetStatefulAppContext& stateful);
}
