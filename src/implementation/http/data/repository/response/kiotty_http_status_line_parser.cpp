#include "http/data/repository/response/kiotty_http_status_line_parser.h"

#include "core/kiotty_codes.h"

#include "core/kiotty_parse_integer.h"
#include "http/data/repository/header/kiotty_http_header_line_parser.h"
#include "http/data/repository/request/kiotty_http_request_line_parser.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using StatusResult = Result<int32_t, KiottyNetCode>;

        constexpr size_t        VERSION_BYTES = 8;
        constexpr size_t        STATUS_DIGITS = 3;
        constexpr size_t        STATUS_START = VERSION_BYTES + 1;
        constexpr size_t        STATUS_END = STATUS_START + STATUS_DIGITS;

        StatusResult parseStatusCode(Span<const char> digits);
        bool         isReasonPart(Span<const char> rest);
    }

    StatusLineResult parseStatusLine(Span<const char> line)
    {
        const bool is_too_short = line.length < STATUS_END;
        if (is_too_short) { return StatusLineResult::failure(PROTOCOL_ERROR); }
        const Span<const char> version_text{ line.data, VERSION_BYTES };
        const HttpVersionResult version = parseHttpVersion(version_text);
        if (!version) { return StatusLineResult::failure(version.code); }
        const bool has_space = line[VERSION_BYTES] == ' ';
        if (!has_space) { return StatusLineResult::failure(PROTOCOL_ERROR); }
        const Span<const char> digits{ line.data + STATUS_START, STATUS_DIGITS };
        const StatusResult status = parseStatusCode(digits);
        if (!status) { return StatusLineResult::failure(status.code); }
        const bool is_reason = isReasonPart(line.skip(STATUS_END));
        if (!is_reason) { return StatusLineResult::failure(PROTOCOL_ERROR); }
        return StatusLineResult::success(StatusLine{ status.value, version.value });
    }

    namespace
    {
        StatusResult parseStatusCode(Span<const char> digits)
        {
            const UnsignedResult parsed = parseUnsigned(digits);
            if (!parsed) { return StatusResult::failure(PROTOCOL_ERROR); }
            const uint64_t first = static_cast<uint64_t>(FIRST_STATUS);
            const uint64_t last = static_cast<uint64_t>(LAST_STATUS);
            const bool is_in_range = parsed.value >= first && parsed.value <= last;
            if (!is_in_range) { return StatusResult::failure(PROTOCOL_ERROR); }
            return StatusResult::success(static_cast<int32_t>(parsed.value));
        }

        bool isReasonPart(Span<const char> rest)
        {
            if (rest.empty()) { return true; }
            if (rest[0] != ' ') { return false; }
            for (size_t i = 1; i < rest.length; ++i)
            {
                if (!isFieldCharacter(rest[i])) { return false; }
            }
            return true;
        }
    }
}
