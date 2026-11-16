#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/response/kiotty_http_reply_framing_rules.h"
#include "http/data/repository/response/kiotty_http_status_line_parser.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class HttpReplyStore
    {
    public:
        HttpReplyStore(Span<char> header_buffer, Span<KiottyNetHttpHeader> headers);

        void                            begin();
        KiottyNetCode                   takeStatusLine(Span<const char> line);
        KiottyNetCode                   takeHeader(Span<const char> line);
        ReplyFramingResult              finish(KiottyNetHttpMethod method) const;
        int32_t                         status() const;
        Span<const KiottyNetHttpHeader> headers() const;

    private:
        Span<char> claimHeaderRoom(Span<const char> line);

        Span<char>                _header_buffer;
        Span<KiottyNetHttpHeader> _headers;
        size_t                    _header_length;
        size_t                    _header_count;
        StatusLine                _status_line;
    };
}
