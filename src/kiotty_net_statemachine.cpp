#include "kiotty_net_statemachine.h"

#include "context/kiotty_context.h"
#include "domain/usecases/statemachine/kiotty_stateful_handler.h"

namespace net = kiotty::net::implementation;

KiottyNetTransition KiottyNet_moveToNextState(int32_t next, KiottyNetConnectionAction entry_action)
{
    KiottyNetTransition transition = {};
    transition.next = next;
    transition.entry_action = entry_action;
    return transition;
}

KiottyNetCode KiottyNet_runStatefulServer(KiottyNetServerHandle server,
                                          const KiottyNetAddress* address,
                                          const KiottyNetTlsServerConfiguration* tls,
                                          const KiottyNetStatefulAppContext* app_context)
{
    if (server == nullptr || address == nullptr || app_context == nullptr)
    {
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }
    const KiottyNetAddress copied = *address;
    const net::StatefulHandler handler(*app_context, *server);
    return server->run(handler, copied, tls);
}

KiottyNetCode KiottyNet_runStatefulEventClient(KiottyNetEventClientHandle client,
                                               const KiottyNetAddress* address,
                                               const KiottyNetTlsClientConfiguration* tls,
                                               const KiottyNetStatefulAppContext* app_context)
{
    if (client == nullptr || address == nullptr || app_context == nullptr)
    {
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }
    const KiottyNetAddress copied = *address;
    const net::StatefulHandler handler(*app_context, *client);
    return client->run(handler, copied, tls);
}
