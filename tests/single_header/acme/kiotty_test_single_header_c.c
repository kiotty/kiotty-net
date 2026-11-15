#include "acme_link.hpp"

#include <stddef.h>

static const AcmeLinkRule RULES[] = {
    ACME_LINK_RULE(0, ACME_LINK_CONNECTION_EVENT_TYPE_READ, NULL, 1, 2),
    ACME_LINK_RULE_UNCHECKED(1, ACME_LINK_CONNECTION_EVENT_TYPE_CLOSED, NULL)
};

int kiottyTestSingleHeaderC(void)
{
    const AcmeLinkStateMachine machine = { RULES, ACME_LINK_RULE_COUNT(RULES), 0 };
    AcmeLinkAddress            address;
    AcmeLinkHttpLimits         limits;
    if (AcmeLink_initializeTcpAddress(&address, "127.0.0.1", 8080) != ACME_LINK_CODE_SUCCESS)
    {
        return 0;
    }
    if (AcmeLink_initializeHttpLimits(&limits) != ACME_LINK_CODE_SUCCESS)
    {
        return 0;
    }
    return address.port == 8080 && machine.count == 2;
}
