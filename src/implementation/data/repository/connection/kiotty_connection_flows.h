#pragma once

#include "data/repository/connection/kiotty_receive_flow.h"
#include "data/repository/connection/kiotty_send_flow.h"
#include "data/repository/connection/kiotty_work_flow.h"

namespace kiotty::net::implementation
{
    struct ConnectionFlows
    {
        ReceiveFlow& receive;
        SendFlow&    send;
        WorkFlow&    work;
    };

    static_assert(sizeof(ConnectionFlows) == 3 * sizeof(void*),
                  "ConnectionFlows: unexpected padding");
}
