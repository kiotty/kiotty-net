#pragma once

#include "base/data/infra/event/kiotty_event_listener.h"
#include "base/data/infra/socket/kiotty_active_socket.h"
#include "base/data/infra/socket/kiotty_passive_socket.h"
#include "base/data/infra/socket/kiotty_socket_transfer.h"
#include "base/data/repository/connection/kiotty_io_source.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class Acceptor : public IoSource
    {
    public:
        Acceptor();
        ~Acceptor() override;

        Acceptor(const Acceptor&) = delete;
        Acceptor& operator=(const Acceptor&) = delete;
        Acceptor(Acceptor&&) = delete;
        Acceptor& operator=(Acceptor&&) = delete;

        SocketCode         open(const KiottyNetAddress& address, int backlog, NoDelay no_delay);
        void               close();
        ActiveSocketResult accept();
        bool               listening() const;
        bool               isPending() const;
        uint16_t           port() const;

        SocketHandle socket() const override;
        int32_t      slot() const override;
        IoEvent&     eventFor(const IoAction& action) override;
        IoPayload    resolve(const IoAction& action) override;
        SocketCode   attach(EventListener& listener) override;
        void         detach(EventListener& listener) override;
        void         shutdown() override;

    private:
        IoPayload acceptPayload();

        AcceptBuffer  _buffer;
        IoEvent       _event;
        ActiveSocket  _prepared;
        PassiveSocket _socket;
        NoDelay       _no_delay;
    };
}
