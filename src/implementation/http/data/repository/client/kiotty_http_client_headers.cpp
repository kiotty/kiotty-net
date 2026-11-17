#include "http/data/repository/client/kiotty_http_client_headers.h"

#include "core/kiotty_busy_guard.h"

namespace kiotty::net::implementation
{
    HttpClientHeaders::HttpClientHeaders(HttpClientState& state)
        : _state(state)
    {
    }

    KiottyNetCode HttpClientHeaders::set(Span<const char> name, Span<const char> value)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        return _state.shared.set(name, value);
    }

    KiottyNetCode HttpClientHeaders::remove(Span<const char> name)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        return _state.shared.remove(name);
    }

    KiottyNetCode HttpClientHeaders::clear()
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        _state.shared.clear();
        return KIOTTY_NET_CODE_SUCCESS;
    }
}
