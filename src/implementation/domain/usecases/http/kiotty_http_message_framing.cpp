#include "domain/usecases/http/kiotty_http_message_framing.h"

#include "core/kiotty_parse_integer.h"
#include "domain/usecases/http/kiotty_http_token.h"

namespace kiotty::net::implementation
{
    namespace
    {
        struct FramingHeaders
        {
            Span<const char> transfer_encoding;
            Span<const char> content_length;
            size_t           transfer_encoding_count;
            size_t           content_length_count;
        };

        enum class Option : uint8_t
        {
            NONE,
            CLOSE,
            KEEP_ALIVE
        };

        constexpr KiottyNetHttpStatus BAD_REQUEST = KIOTTY_NET_HTTP_STATUS_BAD_REQUEST;
        constexpr KiottyNetHttpStatus NOT_IMPLEMENTED = KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED;
        constexpr char TRANSFER_ENCODING[] = "Transfer-Encoding";
        constexpr char CONTENT_LENGTH[] = "Content-Length";
        constexpr char CONNECTION[] = "Connection";
        constexpr char CHUNKED[] = "chunked";
        constexpr char CLOSE_OPTION[] = "close";
        constexpr char KEEP_ALIVE_OPTION[] = "keep-alive";

        FramingHeaders collectFraming(Span<const KiottyNetHttpHeader> headers);
        FramingResult  frameByEncoding(const FramingHeaders& found);
        FramingResult  frameByLength(const FramingHeaders& found);
        Option         findOption(Span<const KiottyNetHttpHeader> headers);
        Option         findOptionIn(Span<const char> value);
        Option         optionOf(Span<const char> word);
        bool           isExtensionText(Span<const char> text);
    }

    LengthResult parseContentLength(Span<const char> value)
    {
        const UnsignedResult parsed = parseUnsigned(value);
        if (!parsed) { return LengthResult::failure(BAD_REQUEST); }
        return LengthResult::success(parsed.value);
    }

    LengthResult parseChunkSize(Span<const char> line)
    {
        const size_t mark = findCharacter(line, ';', 0);
        const Span<const char> digits{ line.data, mark };
        const Span<const char> extension = line.skip(mark);
        if (!isExtensionText(extension)) { return LengthResult::failure(BAD_REQUEST); }
        const UnsignedResult parsed = parseHexadecimal(digits);
        if (!parsed) { return LengthResult::failure(BAD_REQUEST); }
        return LengthResult::success(parsed.value);
    }

    FramingResult selectBodyFraming(Span<const KiottyNetHttpHeader> headers)
    {
        const FramingHeaders found = collectFraming(headers);
        if (found.transfer_encoding_count > 0) { return frameByEncoding(found); }
        return frameByLength(found);
    }

    Persistence decidePersistence(HttpVersion version, Span<const KiottyNetHttpHeader> headers)
    {
        const Option option = findOption(headers);
        if (option == Option::CLOSE) { return Persistence::CLOSE; }
        if (option == Option::KEEP_ALIVE) { return Persistence::KEEP_ALIVE; }
        if (version == HttpVersion::V11) { return Persistence::KEEP_ALIVE; }
        return Persistence::CLOSE;
    }

    namespace
    {
        FramingHeaders collectFraming(Span<const KiottyNetHttpHeader> headers)
        {
            FramingHeaders found{};
            for (const KiottyNetHttpHeader& header : headers)
            {
                const Span<const char> name = spanOf(header.name);
                const Span<const char> value = spanOf(header.value);
                const bool is_encoding = equalsLiteralIgnoreCase(name, TRANSFER_ENCODING);
                const bool is_length = equalsLiteralIgnoreCase(name, CONTENT_LENGTH);
                found.transfer_encoding = is_encoding ? value : found.transfer_encoding;
                found.content_length = is_length ? value : found.content_length;
                found.transfer_encoding_count += is_encoding ? 1 : 0;
                found.content_length_count += is_length ? 1 : 0;
            }
            return found;
        }

        FramingResult frameByEncoding(const FramingHeaders& found)
        {
            const bool has_length = found.content_length_count > 0;
            const bool is_ambiguous = has_length || found.transfer_encoding_count > 1;
            if (is_ambiguous) { return FramingResult::failure(BAD_REQUEST); }
            const bool is_chunked = equalsLiteralIgnoreCase(found.transfer_encoding, CHUNKED);
            if (!is_chunked) { return FramingResult::failure(NOT_IMPLEMENTED); }
            return FramingResult::success(BodyFraming{ 0, BodyKind::CHUNKED });
        }

        FramingResult frameByLength(const FramingHeaders& found)
        {
            const BodyFraming no_body{ 0, BodyKind::NONE };
            if (found.content_length_count == 0) { return FramingResult::success(no_body); }
            if (found.content_length_count > 1) { return FramingResult::failure(BAD_REQUEST); }
            const LengthResult length = parseContentLength(found.content_length);
            if (!length) { return FramingResult::failure(length.code); }
            return FramingResult::success(BodyFraming{ length.value, BodyKind::LENGTH });
        }

        Option findOption(Span<const KiottyNetHttpHeader> headers)
        {
            Option chosen = Option::NONE;
            for (const KiottyNetHttpHeader& header : headers)
            {
                const bool is_connection = equalsLiteralIgnoreCase(spanOf(header.name), CONNECTION);
                const Span<const char> value = spanOf(header.value);
                const Option option = is_connection ? findOptionIn(value) : Option::NONE;
                if (option == Option::CLOSE) { return option; }
                chosen = option == Option::KEEP_ALIVE ? option : chosen;
            }
            return chosen;
        }

        Option findOptionIn(Span<const char> value)
        {
            Option chosen = Option::NONE;
            size_t position = 0;
            while (position < value.length)
            {
                const size_t comma = findCharacter(value, ',', position);
                const Span<const char> word{ value.data + position, comma - position };
                const Option option = optionOf(trimWhitespace(word));
                if (option == Option::CLOSE) { return option; }
                chosen = option == Option::KEEP_ALIVE ? option : chosen;
                position = comma + 1;
            }
            return chosen;
        }

        Option optionOf(Span<const char> word)
        {
            if (equalsLiteralIgnoreCase(word, CLOSE_OPTION)) { return Option::CLOSE; }
            if (equalsLiteralIgnoreCase(word, KEEP_ALIVE_OPTION)) { return Option::KEEP_ALIVE; }
            return Option::NONE;
        }

        bool isExtensionText(Span<const char> text)
        {
            for (size_t i = 0; i < text.length; ++i)
            {
                const bool is_allowed = isVisibleCharacter(text[i]) || isWhitespace(text[i]);
                if (!is_allowed) { return false; }
            }
            return true;
        }
    }
}
