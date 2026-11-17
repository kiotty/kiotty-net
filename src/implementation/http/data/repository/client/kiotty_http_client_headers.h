#pragma once

#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    class HttpClientHeaders
    {
    public:
        explicit HttpClientHeaders(HttpClientState& state);

        KiottyNetCode set(Span<const char> name, Span<const char> value);
        KiottyNetCode remove(Span<const char> name);
        KiottyNetCode clear();

    private:
        HttpClientState& _state;
    };
}
