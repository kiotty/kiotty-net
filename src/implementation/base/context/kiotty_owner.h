#if !defined(KIOTTY_OWNER_H)
#define KIOTTY_OWNER_H

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct KiottyNetConnectionOwner
{
    KiottyNetConnectionOwner() = default;
    KiottyNetConnectionOwner(const KiottyNetConnectionOwner&) = delete;
    KiottyNetConnectionOwner& operator=(const KiottyNetConnectionOwner&) = delete;
    virtual ~KiottyNetConnectionOwner();

    virtual KiottyNetCode           notify(int64_t id, KiottyNetSpan data) = 0;
    virtual KiottyNetConnectionInfo info(int64_t id) const = 0;
    virtual KiottyNetResult         readStream(int64_t id, void* buffer, size_t length, uint32_t timeout_milliseconds) = 0;
    virtual KiottyNetResult         writeStream(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds) = 0;
    virtual KiottyNetCode           writeStreamFile(int64_t id, const char* path, uint64_t offset, uint64_t length, uint32_t timeout_milliseconds) = 0;
    virtual bool                    isStreamOpen(int64_t id) = 0;
    virtual KiottyNetCode           closeStream(int64_t id, const KiottyNetConnectionAction& then) = 0;
};

namespace kiotty::net::implementation
{
    typedef ::KiottyNetConnectionOwner Owner;
}

#endif
