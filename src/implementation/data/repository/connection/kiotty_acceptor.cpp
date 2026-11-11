#include "data/repository/connection/kiotty_acceptor.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t ACCEPTOR_SLOT = -1;
    }

    Acceptor::Acceptor()
        : _buffer{ INVALID_SOCKET_HANDLE, {} }
        , _event()
        , _prepared()
        , _socket()
        , _no_delay(NoDelay::DISABLED)
    {
    }

    Acceptor::~Acceptor()
    {
        close();
    }

    SocketCode Acceptor::open(const KiottyNetAddress& address, int backlog, NoDelay no_delay)
    {
        close();
        _no_delay = no_delay;
        return _socket.open(address, backlog);
    }

    void Acceptor::close()
    {
        _prepared.close();
        _socket.close();
        _buffer.socket = INVALID_SOCKET_HANDLE;
    }

    ActiveSocketResult Acceptor::accept()
    {
        _buffer.socket = INVALID_SOCKET_HANDLE;
        ActiveSocketResult accepted = _socket.accept(std::move(_prepared));
        if (!accepted) { return accepted; }
        const SocketCode configured = configureNoDelay(accepted.value, _no_delay);
        if (configured != SocketCode::SUCCESS) { return ActiveSocketResult::failure(configured); }
        return accepted;
    }

    bool Acceptor::listening() const
    {
        return _socket.valid();
    }

    bool Acceptor::isPending() const
    {
        return _event.pending;
    }

    uint16_t Acceptor::port() const
    {
        return _socket.port();
    }

    SocketHandle Acceptor::socket() const
    {
        return _socket.handle();
    }

    int32_t Acceptor::slot() const
    {
        return ACCEPTOR_SLOT;
    }

    IoEvent& Acceptor::eventFor(const IoAction& action)
    {
        (void)action;
        return _event;
    }

    IoPayload Acceptor::resolve(const IoAction& action)
    {
        switch (action.kind)
        {
        case IoActionKind::ACCEPT:
            return acceptPayload();
        case IoActionKind::NONE:
        case IoActionKind::HOLD:
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
        case IoActionKind::WRITE:
        case IoActionKind::START_WORK:
        case IoActionKind::CLOSE:
            return IoPayload::empty();
        }
        return IoPayload::empty();
    }

    SocketCode Acceptor::attach(EventListener& listener)
    {
        return listener.attach(_event, socket(), slot());
    }

    void Acceptor::detach(EventListener& listener)
    {
        listener.detach(_event);
    }

    void Acceptor::shutdown()
    {
        close();
    }

    IoPayload Acceptor::acceptPayload()
    {
        const bool has_prepared = _prepared.valid();
        if (!has_prepared)
        {
            ActiveSocketResult prepared = _socket.prepare();
            if (prepared) { _prepared = std::move(prepared.value); }
        }
        _buffer.socket = _prepared.handle();
        return IoPayload::single(&_buffer, sizeof(_buffer));
    }
}
