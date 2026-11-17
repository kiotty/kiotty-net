#pragma once

#include "core/kiotty_span.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    void    applyMask(Span<char> data, const MaskKey& key, size_t offset);
    MaskKey nextMaskKey(std::atomic<uint64_t>& state);
}
