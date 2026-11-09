#include "context/kiotty_configuration.h"

#include "data/datasource/kiotty_memory_limits.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   DEFAULT_MAX_CLIENTS = 1024;
        constexpr size_t   DEFAULT_CAPACITY_BYTES = 4096;
        constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MILLISECONDS = 5000;
        constexpr uint32_t DEFAULT_MAX_TRIAL = 1;
        constexpr uint32_t DEFAULT_TRIAL_INTERVAL_MILLISECONDS = 1000;

        struct Capacities
        {
            size_t receive;
            size_t send;
            size_t push;
        };

        static_assert(sizeof(Capacities) == 3 * sizeof(size_t), "Capacities: unexpected padding");

        KiottyNetAddress            addressOf(KiottyNetTransport transport,
                                                const char* name,
                                                size_t length,
                                                uint16_t port);
        KiottyNetServerConfiguration defaultServer(const KiottyNetAddress& address);
        KiottyNetClientConfiguration defaultClient(const KiottyNetAddress& address);
        Capacities                   defaultCapacities();
        bool                         areCapacitiesValid(Capacities capacities);
        bool                         isWithin(size_t value, size_t maximum);
        bool                         isNoDelayKnown(KiottyNetNoDelay no_delay);
        size_t                       smallerOf(size_t left, size_t right);
    }

    KiottyNetCode initializeTcpServerConfiguration(KiottyNetServerConfiguration& configuration,
                                                   const char* host,
                                                   uint16_t port)
    {
        const TextLength measured = measureText(host, KIOTTY_NET_MAX_HOST_LENGTH);
        if (!measured) { return measured.code; }
        if (port == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetAddress address =
            addressOf(KIOTTY_NET_TRANSPORT_TCP, host, measured.value, port);
        configuration = defaultServer(address);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializePipeServerConfiguration(KiottyNetServerConfiguration& configuration,
                                                    const char* pipe_name)
    {
        const TextLength measured = measureText(pipe_name, KIOTTY_NET_MAX_HOST_LENGTH);
        if (!measured) { return measured.code; }
        const KiottyNetAddress address =
            addressOf(KIOTTY_NET_TRANSPORT_PIPE, pipe_name, measured.value, 0);
        configuration = defaultServer(address);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureServerMaxClients(KiottyNetServerConfiguration& configuration,
                                            size_t max_clients)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_valid = isWithin(max_clients, CONNECTIONS_PER_SERVER_LIMIT);
        if (!is_valid) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.max_clients = max_clients;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureServerBufferCapacity(KiottyNetServerConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Capacities capacities{ receive_capacity, send_capacity, push_capacity };
        if (!areCapacitiesValid(capacities)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.receive_capacity = capacities.receive;
        configuration.send_capacity = capacities.send;
        configuration.push_capacity = capacities.push;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureServerNoDelay(KiottyNetServerConfiguration& configuration,
                                         KiottyNetNoDelay no_delay)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isNoDelayKnown(no_delay)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.no_delay = no_delay;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializeTcpClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                   const char* host,
                                                   uint16_t port)
    {
        const TextLength measured = measureText(host, KIOTTY_NET_MAX_HOST_LENGTH);
        if (!measured) { return measured.code; }
        if (port == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetAddress address =
            addressOf(KIOTTY_NET_TRANSPORT_TCP, host, measured.value, port);
        configuration = defaultClient(address);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializePipeClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                    const char* pipe_name)
    {
        const TextLength measured = measureText(pipe_name, KIOTTY_NET_MAX_HOST_LENGTH);
        if (!measured) { return measured.code; }
        const KiottyNetAddress address =
            addressOf(KIOTTY_NET_TRANSPORT_PIPE, pipe_name, measured.value, 0);
        configuration = defaultClient(address);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureClientBufferCapacity(KiottyNetClientConfiguration& configuration,
                                                size_t receive_capacity,
                                                size_t send_capacity,
                                                size_t push_capacity)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Capacities capacities{ receive_capacity, send_capacity, push_capacity };
        if (!areCapacitiesValid(capacities)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.receive_capacity = capacities.receive;
        configuration.send_capacity = capacities.send;
        configuration.push_capacity = capacities.push;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureClientConnectTimeout(KiottyNetClientConfiguration& configuration,
                                                uint32_t timeout_milliseconds)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.connect_timeout_milliseconds = timeout_milliseconds;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureClientRetry(KiottyNetClientConfiguration& configuration,
                                       uint32_t max_trial,
                                       uint32_t trial_interval_milliseconds)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (max_trial == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.max_trial = max_trial;
        configuration.trial_interval_milliseconds = trial_interval_milliseconds;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureClientNoDelay(KiottyNetClientConfiguration& configuration,
                                         KiottyNetNoDelay no_delay)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isNoDelayKnown(no_delay)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.no_delay = no_delay;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isInitialized(const KiottyNetServerConfiguration& configuration)
    {
        return configuration.initialization_mark == INITIALIZATION_MARK;
    }

    bool isInitialized(const KiottyNetClientConfiguration& configuration)
    {
        return configuration.initialization_mark == INITIALIZATION_MARK;
    }

    const char* hostnameOf(const KiottyNetClientConfiguration& configuration)
    {
        const bool is_tcp = configuration.address.transport == KIOTTY_NET_TRANSPORT_TCP;
        return is_tcp ? configuration.address.host : "";
    }

    TextLength measureText(const char* text, size_t capacity)
    {
        if (text == nullptr) { return TextLength::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const size_t length = strnlen(text, capacity);
        const bool is_fitting = length > 0 && length < capacity;
        if (!is_fitting) { return TextLength::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return TextLength::success(length);
    }

    void writeText(Span<char> target, const char* text, size_t length)
    {
        target.fill('\0');
        const bool copied = target.copyFrom(Span<const char>{ text, length });
        (void)copied;
    }

    NoDelay noDelayOf(KiottyNetNoDelay no_delay)
    {
        if (no_delay == KIOTTY_NET_NO_DELAY_ON) { return NoDelay::ENABLED; }
        return NoDelay::DISABLED;
    }

    namespace
    {
        bool isNoDelayKnown(KiottyNetNoDelay no_delay)
        {
            switch (no_delay)
            {
            case KIOTTY_NET_NO_DELAY_OFF:
            case KIOTTY_NET_NO_DELAY_ON:
                return true;
            }
            return false;
        }

        KiottyNetAddress addressOf(KiottyNetTransport transport,
                                     const char* name,
                                     size_t length,
                                     uint16_t port)
        {
            KiottyNetAddress address{};
            address.transport = transport;
            writeText(Span<char>{ address.host, sizeof(address.host) }, name, length);
            address.port = port;
            return address;
        }

        KiottyNetServerConfiguration defaultServer(const KiottyNetAddress& address)
        {
            const Capacities capacities = defaultCapacities();
            KiottyNetServerConfiguration configuration{};
            configuration.initialization_mark = INITIALIZATION_MARK;
            configuration.address = address;
            configuration.max_clients = DEFAULT_MAX_CLIENTS;
            configuration.receive_capacity = capacities.receive;
            configuration.send_capacity = capacities.send;
            configuration.push_capacity = capacities.push;
            configuration.no_delay = KIOTTY_NET_NO_DELAY_ON;
            return configuration;
        }

        KiottyNetClientConfiguration defaultClient(const KiottyNetAddress& address)
        {
            const Capacities capacities = defaultCapacities();
            KiottyNetClientConfiguration configuration{};
            configuration.initialization_mark = INITIALIZATION_MARK;
            configuration.address = address;
            configuration.receive_capacity = capacities.receive;
            configuration.send_capacity = capacities.send;
            configuration.push_capacity = capacities.push;
            configuration.connect_timeout_milliseconds = DEFAULT_CONNECT_TIMEOUT_MILLISECONDS;
            configuration.max_trial = DEFAULT_MAX_TRIAL;
            configuration.trial_interval_milliseconds = DEFAULT_TRIAL_INTERVAL_MILLISECONDS;
            configuration.no_delay = KIOTTY_NET_NO_DELAY_ON;
            return configuration;
        }

        Capacities defaultCapacities()
        {
            return Capacities{ smallerOf(DEFAULT_CAPACITY_BYTES, MemoryLimits::RECEIVE_CAPACITY),
                               smallerOf(DEFAULT_CAPACITY_BYTES, MemoryLimits::SEND_CAPACITY),
                               smallerOf(DEFAULT_CAPACITY_BYTES, MemoryLimits::PUSH_CAPACITY) };
        }

        bool areCapacitiesValid(Capacities capacities)
        {
            const bool receive = isWithin(capacities.receive, MemoryLimits::RECEIVE_CAPACITY);
            const bool send = isWithin(capacities.send, MemoryLimits::SEND_CAPACITY);
            const bool push = isWithin(capacities.push, MemoryLimits::PUSH_CAPACITY);
            return receive && send && push;
        }

        bool isWithin(size_t value, size_t maximum)
        {
            return value >= 1 && value <= maximum;
        }

        size_t smallerOf(size_t left, size_t right)
        {
            return left < right ? left : right;
        }
    }
}
