#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    using FrameStartResult = Result<FrameStart, KiottyNetWebSocketStatus>;
    using FrameLengthResult = Result<uint64_t, KiottyNetWebSocketStatus>;

    FrameStartResult  parseFrameStart(Span<const char> bytes);
    size_t            extendedLengthBytesOf(uint8_t length_code);
    FrameLengthResult parseExtendedLength(Span<const char> bytes);
    MaskKey           parseMaskKey(Span<const char> bytes);
    bool              isControl(FrameOpcode opcode);
}
