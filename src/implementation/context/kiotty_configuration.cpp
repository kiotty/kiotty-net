#include "context/kiotty_configuration.h"

#include "core/kiotty_text.h"
#include "data/datasource/kiotty_memory_limits.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   DEFAULT_MAX_CLIENTS = 1024;
        constexpr size_t   DEFAULT_CAPACITY_BYTES = 4096;
        constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MILLISECONDS = 5000;
        constexpr uint32_t DEFAULT_MAX_TRIAL = 1;
        constexpr uint32_t DEFAULT_TRIAL_INTERVAL_MILLISECONDS = 1000;
        constexpr uint32_t DEFAULT_SEND_TIMEOUT_MILLISECONDS = 30000;

        struct Capacities
        {
            size_t receive;
            size_t send;
            size_t push;
        };

        static_assert(sizeof(Capacities) == 3 * sizeof(size_t), "Capacities: unexpected padding");

        using AddressResult = Result<KiottyNetAddress, KiottyNetCode>;

        AddressResult                addressOf(KiottyNetTransport transport,
                                               Span<const char> name,
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
                                                   Span<const char> host,
                                                   uint16_t port)
    {
        if (port == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const AddressResult address = addressOf(KIOTTY_NET_TRANSPORT_TCP, host, port);
        if (!address) { return address.code; }
        configuration = defaultServer(address.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializePipeServerConfiguration(KiottyNetServerConfiguration& configuration,
                                                    Span<const char> pipe_name)
    {
        const AddressResult address = addressOf(KIOTTY_NET_TRANSPORT_PIPE, pipe_name, 0);
        if (!address) { return address.code; }
        configuration = defaultServer(address.value);
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

    KiottyNetCode configureServerSendTimeout(KiottyNetServerConfiguration& configuration,
                                             uint32_t timeout_milliseconds)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.send_timeout_milliseconds = timeout_milliseconds;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializeTcpClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                   Span<const char> host,
                                                   uint16_t port)
    {
        if (port == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const AddressResult address = addressOf(KIOTTY_NET_TRANSPORT_TCP, host, port);
        if (!address) { return address.code; }
        configuration = defaultClient(address.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializePipeClientConfiguration(KiottyNetClientConfiguration& configuration,
                                                    Span<const char> pipe_name)
    {
        const AddressResult address = addressOf(KIOTTY_NET_TRANSPORT_PIPE, pipe_name, 0);
        if (!address) { return address.code; }
        configuration = defaultClient(address.value);
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

    KiottyNetCode configureClientSendTimeout(KiottyNetClientConfiguration& configuration,
                                             uint32_t timeout_milliseconds)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.send_timeout_milliseconds = timeout_milliseconds;
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

    Span<const char> hostnameOf(const KiottyNetClientConfiguration& configuration)
    {
        const bool is_tcp = configuration.address.transport == KIOTTY_NET_TRANSPORT_TCP;
        if (!is_tcp) { return Span<const char>{ configuration.address.host, 0 }; }
        return textOf(configuration.address.host);
    }

    bool writeText(Span<char> field, Span<const char> text)
    {
        field.fill('\0');
        if (field.empty()) { return false; }
        const Span<char> room{ field.data, field.length - 1 };
        return room.copyFrom(text);
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

        AddressResult addressOf(KiottyNetTransport transport,
                                Span<const char> name,
                                uint16_t port)
        {
            if (name.empty()) { return AddressResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            KiottyNetAddress address{};
            address.transport = transport;
            const Span<char> field{ address.host, sizeof(address.host) };
            const bool written = writeText(field, name);
            if (!written) { return AddressResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            address.port = port;
            return AddressResult::success(address);
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
            configuration.send_timeout_milliseconds = DEFAULT_SEND_TIMEOUT_MILLISECONDS;
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
            configuration.send_timeout_milliseconds = DEFAULT_SEND_TIMEOUT_MILLISECONDS;
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
