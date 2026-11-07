#if !defined(KIOTTY_IO_SOURCE_H)
#define KIOTTY_IO_SOURCE_H

#include "data/infra/event/kiotty_event_listener.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class IoSource
    {
    public:
        virtual ~IoSource() = default;

        virtual SocketHandle socket() const = 0;
        virtual int32_t      slot() const = 0;
        virtual IoEvent&     eventFor(const IoAction& action) = 0;
        virtual IoPayload    resolve(const IoAction& action) = 0;
        virtual SocketCode   attach(EventListener& listener) = 0;
        virtual void         detach(EventListener& listener) = 0;
        virtual void         shutdown() = 0;

    protected:
        IoSource() = default;
        IoSource(const IoSource&) = delete;
        IoSource& operator=(const IoSource&) = delete;
    };
}

#endif
