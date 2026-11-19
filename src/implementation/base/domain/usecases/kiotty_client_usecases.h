#pragma once

#include "base/data/repository/endpoint/kiotty_event_client.h"
#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    size_t          clientMemorySize(const KiottyNetClientConfiguration& configuration);
    KiottyNetCode   connectStreamClient(StreamClient& client,
                                        const KiottyNetAddress& address,
                                        const KiottyNetTlsClientConfiguration* tls);
    void            disconnectStreamClient(StreamClient& client);
    KiottyNetCode   cancelStreamClient(StreamClient& client);
    KiottyNetResult readStreamClient(StreamClient& client,
                                     Span<char> buffer,
                                     uint32_t timeout_milliseconds);
    KiottyNetResult writeStreamClient(StreamClient& client,
                                      KiottyNetSpan data,
                                      uint32_t timeout_milliseconds);
    KiottyNetCode   notifyEventClient(EventClient& client, KiottyNetSpan data);
}
