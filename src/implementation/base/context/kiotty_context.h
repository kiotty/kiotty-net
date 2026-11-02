#if !defined(KIOTTY_CONTEXT_H)
#define KIOTTY_CONTEXT_H

#include "core/kiotty_pool.h"

namespace kiotty
{
    namespace net
    {
        class Context
        {
        public:
            Context();

            Context(const Context&) = delete;
            Context& operator=(const Context&) = delete;

            bool  valid() const;
            bool  tlsPooled() const;
            Pool& pool();

        private:
            Pool _pool;
            bool _tls_pooled;
        };
    }
}

#endif
