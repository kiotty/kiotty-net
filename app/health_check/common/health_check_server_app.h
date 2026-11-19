#ifndef KIOTTY_HEALTH_CHECK_SERVER_APP_H
#define KIOTTY_HEALTH_CHECK_SERVER_APP_H

#include "kiotty_net_base.h"

#include <string>

namespace health_check
{
    int runServerApp(const KiottyNetAddress& address, const std::string& where);
}

#endif
