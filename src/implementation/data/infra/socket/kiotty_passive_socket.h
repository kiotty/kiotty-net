#pragma once

#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/socket/kiotty_socket_handle.h"
#include "domain/entity/kiotty_socket_code.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class PassiveSocket
    {
    public:
        PassiveSocket();
        ~PassiveSocket();

        PassiveSocket(const PassiveSocket&) = delete;
        PassiveSocket& operator=(const PassiveSocket&) = delete;

        SocketCode         open(const KiottyNetEndpoint& endpoint, int backlog);
        ActiveSocketResult accept();
        void               close();

        bool         valid() const;
        SocketHandle handle() const;
        uint16_t     port() const;

    private:
        SocketHandle       _handle;
        KiottyNetTransport _transport;
        uint16_t           _port;
    };
}
