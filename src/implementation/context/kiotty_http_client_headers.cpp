#include "context/kiotty_http_client_headers.h"

#include "core/kiotty_busy_guard.h"

namespace kiotty::net::implementation
{
    HttpClientHeaders::HttpClientHeaders(HttpSharedHeaders& shared, std::atomic<bool>& is_busy)
        : _shared(shared)
        , _is_busy(is_busy)
    {
    }

    KiottyNetCode HttpClientHeaders::set(Span<const char> name, Span<const char> value)
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        return _shared.set(name, value);
    }

    KiottyNetCode HttpClientHeaders::remove(Span<const char> name)
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        return _shared.remove(name);
    }

    KiottyNetCode HttpClientHeaders::clear()
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        _shared.clear();
        return KIOTTY_NET_CODE_SUCCESS;
    }
}
