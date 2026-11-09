#pragma once

#include "context/kiotty_event_client.h"
#include "context/kiotty_server.h"
#include "context/kiotty_stream_client.h"

struct KiottyNetServer final : kiotty::net::implementation::Server
{
};

struct KiottyNetStreamClient final : kiotty::net::implementation::StreamClient
{
};

struct KiottyNetEventClient final : kiotty::net::implementation::EventClient
{
};
