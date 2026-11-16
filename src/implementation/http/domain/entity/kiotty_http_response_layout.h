#pragma once

#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class BodyStorage : uint8_t
    {
        COPY,
        REFERENCE,
        STREAM,
        FILE
    };

    struct ResponseSeal
    {
        KiottyNetHttpMethod method;
        Persistence         persistence;
    };

    struct ResponseLayout
    {
        Span<char>               buffer;
        Span<const char>         body;
        uint64_t                 file_length;
        size_t                   header_start;
        size_t                   header_end;
        size_t                   body_start;
        size_t                   sendable_body;
        KiottyNetHttpContentType type;
        BodyStorage              storage;
        bool                     has_body;
    };

    struct FilePiece
    {
        Span<char> target;
        uint64_t   file_offset;
        bool       is_file;
    };
}
