#if !defined(KIOTTY_TLS_MEMORY_H)
#define KIOTTY_TLS_MEMORY_H

#include "core/pool/kiotty_global_pool.h"

#include <cstddef>

namespace kiotty
{
    namespace net
    {
        bool            useTlsGlobalPool();
        size_t          tlsPoolClassCount();
        GlobalPoolUsage tlsPoolUsage(size_t index);
    }
}

#endif
