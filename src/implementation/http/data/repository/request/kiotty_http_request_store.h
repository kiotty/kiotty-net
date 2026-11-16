#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/repository/body/kiotty_http_message_framing_parser.h"
#include "http/domain/entity/kiotty_http_request_line.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    inline constexpr KiottyNetHttpStatus NO_ERROR_STATUS = static_cast<KiottyNetHttpStatus>(0);

    using AppendedBodyResult = Result<Span<char>, KiottyNetHttpStatus>;

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
        AppendedBodyResult          appendBody(Span<const char> data);
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
