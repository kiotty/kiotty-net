#pragma once

#include "context/kiotty_stream_client.h"
#include "core/kiotty_busy_guard.h"
#include "data/infra/socket/kiotty_stop_signal.h"

namespace kiotty::net::implementation
{
    class HttpClientLink
    {
    public:
        HttpClientLink(StreamClient& stream, StopSignal& stop, BusyFlag& busy);

        void disconnect();
        void close();

    private:
        StreamClient& _stream;
        StopSignal&   _stop;
        BusyFlag&     _busy;
    };
}
