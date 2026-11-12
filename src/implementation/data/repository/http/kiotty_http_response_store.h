#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_message_framing.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class BodyStorage : uint8_t
    {
        COPY,
        REFERENCE
    };

    struct ResponseSeal
    {
        KiottyNetHttpMethod method;
        Persistence         persistence;
    };

    class HttpResponseStore
    {
    public:
        explicit HttpResponseStore(Span<char> buffer);

        void             begin();
        KiottyNetCode    setStatus(int32_t status);
        KiottyNetCode    addHeader(Span<const char> name, Span<const char> value);
        KiottyNetCode    setCustomContentType(const char* content_type);
        KiottyNetCode    setBody(KiottyNetHttpContentType type,
                                 KiottyNetSpan data,
                                 BodyStorage storage);
        void             seal(const ResponseSeal& seal);
        Span<const char> chunkAt(size_t offset, size_t max_length) const;

    private:
        using CustomType = FixedString<KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH>;

        KiottyNetCode    checkBodyType(KiottyNetHttpContentType type) const;
        size_t           headerRoom() const;
        Span<const char> contentTypeText() const;
        int32_t          finalStatus() const;

        Span<char>               _buffer;
        Span<const char>         _body;
        CustomType               _custom;
        size_t                   _header_start;
        size_t                   _header_end;
        size_t                   _body_start;
        size_t                   _sendable_body;
        int32_t                  _status;
        KiottyNetHttpContentType _type;
        bool                     _has_body;
        bool                     _is_touched;
    };
}
