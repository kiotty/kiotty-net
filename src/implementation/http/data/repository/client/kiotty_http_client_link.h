#pragma once

#include "base/data/infra/socket/kiotty_stop_signal.h"
#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_busy_guard.h"

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
