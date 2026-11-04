#if !defined(KIOTTY_TLS_MEMORY_H)
#define KIOTTY_TLS_MEMORY_H

#include "core/pool/kiotty_static_pool.h"

#include <cstddef>

namespace kiotty
{
    namespace net
    {
        bool            useTlsStaticPool();
        size_t          tlsPoolClassCount();
        StaticPoolUsage tlsPoolUsage(size_t index);
    }
}

#endif
