#include "kiotty_net.hpp"

#include <stddef.h>

static const KiottyNetRule RULES[] = {
    KIOTTY_NET_RULE(0, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, NULL, 1, 2),
    KIOTTY_NET_RULE_UNCHECKED(1, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED, NULL)
};

int kiottyTestSingleHeaderC(void)
{
    const KiottyNetStateMachine machine = { RULES, KIOTTY_NET_RULE_COUNT(RULES), 0 };
    KiottyNetAddress            address;
    KiottyNetHttpLimits         limits;
    if (KiottyNet_initializeTcpAddress(&address, "127.0.0.1", 8080) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 0;
    }
    if (KiottyNet_initializeHttpLimits(&limits) != KIOTTY_NET_CODE_SUCCESS)
    {
        return 0;
    }
    return address.port == 8080 && machine.count == 2;
}
