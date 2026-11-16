#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_response_layout.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class ResponseFrame
    {
    public:
        explicit ResponseFrame(const ResponseLayout& layout);

        Span<const char> chunkAt(size_t offset, size_t max_length) const;
        KiottyNetCode    checkChunk(Span<const char> data) const;
        bool             isStream() const;
        bool             hasSendableBody() const;
        Span<const char> frameChunk(Span<const char> data, size_t kept, size_t limit) const;
        FilePiece        filePieceAt(size_t offset, size_t max_length) const;

    private:
        const ResponseLayout& _layout;
    };

    class HttpResponseStore
    {
    public:
        explicit HttpResponseStore(Span<char> buffer);

        void          begin();
        KiottyNetCode setStatus(int32_t status);
        KiottyNetCode addHeader(Span<const char> name, Span<const char> value);
        KiottyNetCode setCustomContentType(const char* content_type);
        KiottyNetCode setBody(KiottyNetHttpContentType type,
                              KiottyNetSpan data,
                              BodyStorage storage);
        void          seal(const ResponseSeal& seal);
        ResponseFrame frame() const;

    private:
        using CustomType = FixedString<KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH>;

        KiottyNetCode    checkBodyType(KiottyNetHttpContentType type, BodyStorage storage) const;
        size_t           headerRoom() const;
        Span<const char> contentTypeText() const;
        int32_t          finalStatus() const;

        ResponseLayout _layout;
        CustomType     _custom;
        int32_t        _status;
        bool           _is_touched;
    };
}
