#pragma once

#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_message_framing.h"
#include "domain/usecases/http/kiotty_http_request_line.h"
#include "domain/usecases/websocket/kiotty_websocket_frame.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    inline constexpr KiottyNetHttpStatus NO_ERROR_STATUS = static_cast<KiottyNetHttpStatus>(0);

    class HttpRequestStore
    {
    public:
        HttpRequestStore(Span<char> header_buffer,
                         Span<KiottyNetHttpHeader> headers,
                         Span<char> body_buffer);

        void                        begin(KiottyNetConnectionHandle connection);
        KiottyNetHttpStatus         takeRequestLine(Span<const char> line);
        KiottyNetHttpStatus         takeHeader(Span<const char> line);
        FramingResult               finishHeaders();
        KiottyNetHttpStatus         appendBody(Span<const char> data);
        KiottyNetHttpStatus         appendBody(Span<const char> data,
                                               const MaskKey& key,
                                               size_t offset);
        bool                        expectsContinue() const;
        const KiottyNetHttpRequest& request() const;

    private:
        Span<char> claimHeaderRoom(Span<const char> line);

        KiottyNetHttpRequest      _request;
        Span<char>                _header_buffer;
        Span<KiottyNetHttpHeader> _headers;
        Span<char>                _body_buffer;
        size_t                    _header_length;
        HttpVersion               _version;
    };
}
