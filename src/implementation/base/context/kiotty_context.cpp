#include "base/context/kiotty_context.h"

#include "base/pool/kiotty_memory_layout.h"
#include "base/tls/kiotty_tls_memory.h"

namespace kiotty
{
    namespace net
    {
        Context::Context()
            : _pool(ContextPoolLayout::classes(), ContextPoolLayout::CLASS_COUNT)
            , _tls_pooled(useTlsStaticPool())
        {
        }

        bool Context::valid() const
        {
            return _pool.valid();
        }

        bool Context::tlsPooled() const
        {
            return _tls_pooled;
        }

        Pool& Context::pool()
        {
            return _pool;
        }
    }
}
