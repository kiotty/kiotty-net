#pragma once

#include "kiotty_net_base.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    size_t serverMemorySize(const KiottyNetServerConfiguration& configuration);
}
