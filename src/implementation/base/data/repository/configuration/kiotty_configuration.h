#pragma once

#include "base/data/infra/socket/kiotty_socket_transfer.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr uint32_t INITIALIZATION_MARK = 0x4B4E4554u;

    KiottyNetCode initializeTcpAddress(KiottyNetAddress& address,
                                       Span<const char> host,
                                       uint16_t port);
    KiottyNetCode initializePipeAddress(KiottyNetAddress& address, Span<const char> pipe_name);
    KiottyNetCode setAddressConnectTimeout(KiottyNetAddress& address,
                                           uint32_t timeout_milliseconds);
    KiottyNetCode setAddressRetry(KiottyNetAddress& address,
                                  uint32_t max_trial,
                                  uint32_t trial_interval_milliseconds);
    KiottyNetCode setAddressNoDelay(KiottyNetAddress& address, KiottyNetNoDelay no_delay);
    KiottyNetCode setAddressSendTimeout(KiottyNetAddress& address, uint32_t timeout_milliseconds);
    bool          isAddressValid(const KiottyNetAddress& address);

    KiottyNetCode initializeServerConfiguration(KiottyNetServerConfiguration& configuration);
    KiottyNetCode configureServerMaxClients(KiottyNetServerConfiguration& configuration,
                                            size_t max_clients);
    KiottyNetCode configureServerBufferCapacity(KiottyNetServerConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity);

    KiottyNetCode initializeClientConfiguration(KiottyNetClientConfiguration& configuration);
    KiottyNetCode configureClientBufferCapacity(KiottyNetClientConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity);

    NoDelay          noDelayOf(KiottyNetNoDelay no_delay);
    bool             isInitialized(const KiottyNetServerConfiguration& configuration);
    bool             isInitialized(const KiottyNetClientConfiguration& configuration);
    bool             isInitialized(const KiottyNetAddress& address);
    Span<const char> hostnameOf(const KiottyNetAddress& address);

    bool writeText(Span<char> field, Span<const char> text);
}
