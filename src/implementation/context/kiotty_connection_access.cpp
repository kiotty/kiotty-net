#include "context/kiotty_connection_access.h"

#include "core/pool/kiotty_pool_handle.h"

namespace kiotty::net::implementation
{
    ConnectionAccess::ConnectionAccess(NotificationDesk& desk,
                                       EventListener& listener,
                                       const IoLoop& loop)
        : _desk(desk)
        , _listener(listener)
        , _loop(loop)
    {
    }

    KiottyNetCode ConnectionAccess::notify(int64_t id, KiottyNetSpan data)
    {
        const KiottyNetCode staged = _desk.notify(PoolHandle::fromValue(id), data);
        if (staged != KIOTTY_NET_CODE_SUCCESS) { return staged; }
        (void)_listener.wake();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode ConnectionAccess::notify(int64_t id, Span<const KiottyNetSpan> pieces)
    {
        const KiottyNetCode staged = _desk.notify(PoolHandle::fromValue(id), pieces);
        if (staged != KIOTTY_NET_CODE_SUCCESS) { return staged; }
        (void)_listener.wake();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult ConnectionAccess::readStream(int64_t id,
                                                 Span<char> buffer,
                                                 uint32_t timeout_milliseconds)
    {
        const ConnectionResult found = working(id);
        if (!found) { return KiottyNetResult{ 0, found.code }; }
        return found.value().flows().work.stream().read(buffer, timeout_milliseconds);
    }

    KiottyNetResult ConnectionAccess::writeStream(int64_t id,
                                                  KiottyNetSpan data,
                                                  uint32_t timeout_milliseconds)
    {
        const ConnectionResult found = working(id);
        if (!found) { return KiottyNetResult{ 0, found.code }; }
        return found.value().flows().work.stream().write(data, timeout_milliseconds);
    }

    KiottyNetCode ConnectionAccess::writeStreamFile(int64_t id,
                                                    Span<const char> path,
                                                    FileRange range,
                                                    uint32_t timeout_milliseconds)
    {
        const ConnectionResult found = working(id);
        if (!found) { return found.code; }
        ConnectionStream stream = found.value().flows().work.stream();
        return stream.writeFile(path, range.offset, range.length, timeout_milliseconds);
    }

    bool ConnectionAccess::isStreamOpen(int64_t id)
    {
        const ConnectionResult found = working(id);
        if (!found) { return false; }
        return found.value().flows().work.stream().connected();
    }

    KiottyNetCode ConnectionAccess::closeStream(int64_t id, const KiottyNetConnectionAction& then)
    {
        const KiottyNetCode closed = _desk.closeStream(PoolHandle::fromValue(id), then);
        (void)_listener.wake();
        return closed;
    }

    ConnectionResult ConnectionAccess::working(int64_t id)
    {
        if (_loop.isIoThread()) { return ConnectionResult::failure(KIOTTY_NET_CODE_BUSY); }
        return _desk.working(PoolHandle::fromValue(id));
    }
}
