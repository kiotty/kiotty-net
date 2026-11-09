#include "data/infra/event/kiotty_event_listener.h"

#include <thread>

namespace kiotty::net::implementation
{
    SocketCode EventListener::open(size_t completion_capacity)
    {
        const SocketCode opened = openBackend(completion_capacity);
        if (opened == SocketCode::SUCCESS)
        {
            _is_open.store(true);
        }
        return opened;
    }

    void EventListener::close()
    {
        _is_open.store(false);
        while (_wakers.load() != 0)
        {
            std::this_thread::yield();
        }
        closeBackend();
    }

    SocketCode EventListener::wake()
    {
        _wakers.fetch_add(1);
        SocketCode woken = SocketCode::CLOSED;
        if (_is_open.load())
        {
            woken = wakeBackend();
        }
        _wakers.fetch_sub(1);
        return woken;
    }
}
