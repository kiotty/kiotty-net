#ifndef KIOTTY_HEALTH_CHECK_CLIENT_APP_H
#define KIOTTY_HEALTH_CHECK_CLIENT_APP_H

#include "kiotty_net_base.h"

#include <string>

namespace health_check
{
    int runClientApp(const KiottyNetAddress& address, const std::string& where);
}

#endif
