#pragma once

#include "core/kiotty_span.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    FrameHead        buildFrameHead(FrameOpcode opcode, uint64_t length);
    FrameHead        buildMaskedFrameHead(FrameOpcode opcode, uint64_t length, const MaskKey& key);
    Span<const char> headBytesOf(const FrameHead& head);
}
