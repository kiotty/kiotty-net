#pragma once

#include "core/kiotty_result.h"
#include "data/infra/socket/kiotty_socket_handle.h"
#include "domain/entity/kiotty_socket_code.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class ActiveSocket;

    using ActiveSocketResult = Result<ActiveSocket, SocketCode>;
    using TransferResult = Result<size_t, SocketCode>;

    class ActiveSocket
    {
    public:
        ActiveSocket();
        ~ActiveSocket();

        ActiveSocket(ActiveSocket&& other) noexcept;
        ActiveSocket& operator=(ActiveSocket&& other) noexcept;

        ActiveSocket(const ActiveSocket&) = delete;
        ActiveSocket& operator=(const ActiveSocket&) = delete;

        static ActiveSocketResult open(KiottyNetTransport transport);

        SocketCode connect(const KiottyNetAddress& address, uint32_t timeout_milliseconds);
        void       close();

        bool                    valid() const;
        SocketHandle            handle() const;
        KiottyNetTransport      transport() const;
        KiottyNetConnectionInfo peer() const;

    private:
        friend class PassiveSocket;

        ActiveSocket(SocketHandle handle, KiottyNetTransport transport);

        KiottyNetConnectionInfo _peer;
        SocketHandle            _handle;
        KiottyNetTransport      _transport;
    };
}
