#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "http/data/state/kiotty_http_client_state.h"

namespace kiotty::net::implementation
{
    class HttpClientLink
    {
    public:
        HttpClientLink(HttpClientState& state, StreamClient& line);

        void disconnect();
        void close();

    private:
        HttpClientState& _state;
        StreamClient&    _line;
    };
}
