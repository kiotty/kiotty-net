#include "http/data/repository/header/kiotty_http_header_line_parser.h"

#include "http/domain/entity/kiotty_http_constants.h"

#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr unsigned char       FIRST_NON_ASCII = 0x80;
    }

    HeaderResult parseHeaderLine(Span<const char> line, size_t max_length)
    {
        const bool is_too_long = line.length > max_length;
        if (is_too_long) { return HeaderResult::failure(TOO_LARGE); }
        const size_t name_end = skipToken(line, 0);
        const bool has_colon = name_end > 0 && name_end < line.length && line[name_end] == ':';
        if (!has_colon) { return HeaderResult::failure(BAD_REQUEST); }
        const Span<const char> value = trimWhitespace(line.skip(name_end + 1));
        if (!isFieldValue(value)) { return HeaderResult::failure(BAD_REQUEST); }
        const KiottyNetHttpHeader header{
            KiottyNetSpan{ line.data, name_end },
            KiottyNetSpan{ value.data, value.length },
        };
        return HeaderResult::success(header);
    }

    bool isFieldValue(Span<const char> value)
    {
        for (size_t i = 0; i < value.length; ++i)
        {
            if (!isFieldCharacter(value[i])) { return false; }
        }
        return isUtf8(value);
    }

    bool isFieldCharacter(char character)
    {
        if (isVisibleCharacter(character)) { return true; }
        if (isWhitespace(character)) { return true; }
        return static_cast<unsigned char>(character) >= FIRST_NON_ASCII;
    }
}
