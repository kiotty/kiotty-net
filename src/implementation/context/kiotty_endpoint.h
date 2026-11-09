#pragma once

#include "core/kiotty_span.h"
#include "data/infra/file/kiotty_input_file.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct KiottyNetEndpoint
{
    KiottyNetEndpoint() = default;
    KiottyNetEndpoint(const KiottyNetEndpoint&) = delete;
    KiottyNetEndpoint& operator=(const KiottyNetEndpoint&) = delete;
    virtual ~KiottyNetEndpoint();

    using FileRange = kiotty::net::implementation::FileRange;

    virtual KiottyNetCode           notify(int64_t id, KiottyNetSpan data) = 0;
    virtual KiottyNetConnectionInfo info(int64_t id) const = 0;
    virtual KiottyNetResult         readStream(int64_t id,
                                               kiotty::net::implementation::Span<char> buffer,
                                               uint32_t timeout_milliseconds) = 0;
    virtual KiottyNetResult         writeStream(int64_t id,
                                                KiottyNetSpan data,
                                                uint32_t timeout_milliseconds) = 0;
    virtual KiottyNetCode           writeStreamFile(int64_t id,
                                                    const char* path,
                                                    FileRange range,
                                                    uint32_t timeout_milliseconds) = 0;
    virtual bool                    isStreamOpen(int64_t id) = 0;
    virtual KiottyNetCode           closeStream(int64_t id,
                                                const KiottyNetConnectionAction& then) = 0;
};

namespace kiotty::net::implementation
{
    using Endpoint = ::KiottyNetEndpoint;
}
