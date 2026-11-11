#pragma once

#include "core/kiotty_span.h"
#include "data/infra/socket/kiotty_socket_transfer.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr uint32_t INITIALIZATION_MARK = 0x4B4E4554u;

    KiottyNetCode initializeTcpServerConfiguration(KiottyNetServerConfiguration& configuration,
                                                   Span<const char> host,
                                                   uint16_t port);
    KiottyNetCode initializePipeServerConfiguration(KiottyNetServerConfiguration& configuration,
                                                    Span<const char> pipe_name);
    KiottyNetCode configureServerMaxClients(KiottyNetServerConfiguration& configuration,
                                            size_t max_clients);
    KiottyNetCode configureServerBufferCapacity(KiottyNetServerConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity);
    KiottyNetCode configureServerNoDelay(KiottyNetServerConfiguration& configuration,
                                         KiottyNetNoDelay no_delay);
    KiottyNetCode configureServerSendTimeout(KiottyNetServerConfiguration& configuration,
                                             uint32_t timeout_milliseconds);

    KiottyNetCode initializeTcpClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                   Span<const char> host,
                                                   uint16_t port);
    KiottyNetCode initializePipeClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                    Span<const char> pipe_name);
    KiottyNetCode configureClientBufferCapacity(KiottyNetClientConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity);
    KiottyNetCode configureClientConnectTimeout(KiottyNetClientConfiguration& configuration,
                                                uint32_t timeout_milliseconds);
    KiottyNetCode configureClientRetry(KiottyNetClientConfiguration& configuration,
                                       uint32_t max_trial,
                                       uint32_t trial_interval_milliseconds);
    KiottyNetCode configureClientNoDelay(KiottyNetClientConfiguration& configuration,
                                         KiottyNetNoDelay no_delay);
    KiottyNetCode configureClientSendTimeout(KiottyNetClientConfiguration& configuration,
                                             uint32_t timeout_milliseconds);

    NoDelay          noDelayOf(KiottyNetNoDelay no_delay);
    bool             isInitialized(const KiottyNetServerConfiguration& configuration);
    bool             isInitialized(const KiottyNetClientConfiguration& configuration);
    Span<const char> hostnameOf(const KiottyNetClientConfiguration& configuration);

    bool writeText(Span<char> field, Span<const char> text);
}
