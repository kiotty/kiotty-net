#pragma once

#include "core/kiotty_span.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/file/kiotty_input_file.h"
#include "data/repository/connection/kiotty_notification_desk.h"
#include "domain/usecases/io_loop/kiotty_io_loop.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class ConnectionAccess
    {
    public:
        ConnectionAccess(NotificationDesk& desk, EventListener& listener, const IoLoop& loop);

        KiottyNetCode   notify(int64_t id, KiottyNetSpan data);
        KiottyNetCode   notify(int64_t id, Span<const KiottyNetSpan> pieces);
        KiottyNetResult readStream(int64_t id, Span<char> buffer, uint32_t timeout_milliseconds);
        KiottyNetResult writeStream(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds);
        KiottyNetCode   writeStreamFile(int64_t id,
                                        Span<const char> path,
                                        FileRange range,
                                        uint32_t timeout_milliseconds);
        bool            isStreamOpen(int64_t id);
        KiottyNetCode   closeStream(int64_t id, const KiottyNetConnectionAction& then);

    private:
        ConnectionResult working(int64_t id);

        NotificationDesk& _desk;
        EventListener&    _listener;
        const IoLoop&     _loop;
    };
}
