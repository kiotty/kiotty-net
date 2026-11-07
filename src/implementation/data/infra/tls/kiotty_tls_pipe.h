#pragma once

#include "data/infra/tls/kiotty_tls_engine.h"
#include "core/kiotty_span.h"
#include "domain/entity/kiotty_tls_step.h"
#include "kiotty_net_base.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    class TlsPipe
    {
    public:
        explicit TlsPipe(TlsEngine& engine);

        TlsStep          write(KiottyNetSpan plain);
        Span<char>       receiveRoom();
        void             received(size_t length);
        Span<const char> outgoing();
        void             sent(size_t length);

#if defined(KIOTTY_NET_TESTING)
        void requestKeyUpdate();
        void sendCloseNotify();
#endif

    private:
        TlsEngine& _engine;
    };
}
