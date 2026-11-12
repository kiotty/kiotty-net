#include "domain/usecases/http/kiotty_http_response_text.h"

#include "kiotty_net_http.h"

#include <string>

namespace kiotty::net::implementation
{
    namespace
    {
        struct ReasonPhrase
        {
            int32_t     status;
            const char* phrase;
        };

        constexpr ReasonPhrase REASON_PHRASES[] = {
            { 100, "Continue" },
            { 101, "Switching Protocols" },
            { 200, "OK" },
            { 201, "Created" },
            { 202, "Accepted" },
            { 204, "No Content" },
            { 206, "Partial Content" },
            { 301, "Moved Permanently" },
            { 302, "Found" },
            { 303, "See Other" },
            { 304, "Not Modified" },
            { 307, "Temporary Redirect" },
            { 308, "Permanent Redirect" },
            { 400, "Bad Request" },
            { 401, "Unauthorized" },
            { 403, "Forbidden" },
            { 404, "Not Found" },
            { 405, "Method Not Allowed" },
            { 406, "Not Acceptable" },
            { 408, "Request Timeout" },
            { 409, "Conflict" },
            { 410, "Gone" },
            { 411, "Length Required" },
            { 412, "Precondition Failed" },
            { 413, "Content Too Large" },
            { 414, "URI Too Long" },
            { 415, "Unsupported Media Type" },
            { 416, "Range Not Satisfiable" },
            { 422, "Unprocessable Content" },
            { 426, "Upgrade Required" },
            { 429, "Too Many Requests" },
            { 431, "Request Header Fields Too Large" },
            { 500, "Internal Server Error" },
            { 501, "Not Implemented" },
            { 502, "Bad Gateway" },
            { 503, "Service Unavailable" },
            { 504, "Gateway Timeout" },
            { 505, "HTTP Version Not Supported" },
        };

        constexpr const char* METHOD_NAMES[KIOTTY_NET_HTTP_METHOD_COUNT] = {
            "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS",
        };
        constexpr const char* WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        constexpr const char* MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        constexpr size_t      DECIMAL_DIGITS = 20;
        constexpr uint64_t    DECIMAL_BASE = 10;
        constexpr int32_t     LAST_WEEKDAY = 6;
        constexpr int32_t     LAST_MONTH = 12;

        using DigitText = FixedString<DECIMAL_DIGITS + 1>;

        Span<const char> literalOf(const char* text);
        Span<const char> reasonPhraseOf(int32_t status);
        DigitText        decimalOf(uint64_t value);
        DigitText        paddedOf(int32_t value);
        bool             appendDate(PrefixText& text, const CalendarTime& time);
        bool             appendClock(PrefixText& text, const CalendarTime& time);
        uint32_t         withImplied(uint32_t methods);
    }

    bool appendStatusLine(PrefixText& text, int32_t status)
    {
        const DigitText code = decimalOf(static_cast<uint64_t>(status));
        bool is_written = text.append(literalOf("HTTP/1.1 "));
        is_written = is_written && text.append(code.span());
        is_written = is_written && text.append(literalOf(" "));
        is_written = is_written && text.append(reasonPhraseOf(status));
        return is_written && text.append(literalOf("\r\n"));
    }

    bool appendField(PrefixText& text, Span<const char> name, Span<const char> value)
    {
        bool is_written = text.append(name);
        is_written = is_written && text.append(literalOf(": "));
        is_written = is_written && text.append(value);
        return is_written && text.append(literalOf("\r\n"));
    }

    bool appendLengthField(PrefixText& text, uint64_t length)
    {
        const DigitText digits = decimalOf(length);
        return appendField(text, literalOf("Content-Length"), digits.span());
    }

    bool appendDateField(PrefixText& text, const CalendarTime& time)
    {
        bool is_written = text.append(literalOf("Date: "));
        is_written = is_written && appendDate(text, time);
        is_written = is_written && appendClock(text, time);
        return is_written && text.append(literalOf(" GMT\r\n"));
    }

    AllowText allowListOf(uint32_t methods)
    {
        AllowText text;
        const uint32_t allowed = withImplied(methods);
        for (int32_t method = 0; method < KIOTTY_NET_HTTP_METHOD_COUNT; ++method)
        {
            const bool is_allowed = (allowed & KIOTTY_NET_HTTP_METHOD_BIT(method)) != 0;
            const bool needs_comma = is_allowed && !text.empty();
            if (needs_comma) { (void)text.append(literalOf(", ")); }
            if (is_allowed) { (void)text.append(literalOf(METHOD_NAMES[method])); }
        }
        return text;
    }

    namespace
    {
        Span<const char> literalOf(const char* text)
        {
            return Span<const char>{ text, std::char_traits<char>::length(text) };
        }

        Span<const char> reasonPhraseOf(int32_t status)
        {
            for (const ReasonPhrase& reason : REASON_PHRASES)
            {
                if (reason.status == status) { return literalOf(reason.phrase); }
            }
            return literalOf("");
        }

        DigitText decimalOf(uint64_t value)
        {
            char digits[DECIMAL_DIGITS] = {};
            size_t start = DECIMAL_DIGITS;
            uint64_t rest = value;
            do
            {
                start -= 1;
                digits[start] = static_cast<char>('0' + rest % DECIMAL_BASE);
                rest /= DECIMAL_BASE;
            } while (rest > 0);
            DigitText text;
            (void)text.assign(Span<const char>{ digits + start, DECIMAL_DIGITS - start });
            return text;
        }

        DigitText paddedOf(int32_t value)
        {
            const bool is_single = value >= 0 && value < static_cast<int32_t>(DECIMAL_BASE);
            DigitText text;
            if (is_single) { (void)text.append(literalOf("0")); }
            const DigitText digits = decimalOf(static_cast<uint64_t>(value));
            (void)text.append(digits.span());
            return text;
        }

        bool appendDate(PrefixText& text, const CalendarTime& time)
        {
            const bool is_valid = time.weekday >= 0 && time.weekday <= LAST_WEEKDAY
                                  && time.month >= 1 && time.month <= LAST_MONTH;
            if (!is_valid) { return false; }
            const DigitText day = paddedOf(time.day);
            const DigitText year = decimalOf(static_cast<uint64_t>(time.year));
            bool is_written = text.append(literalOf(WEEKDAYS[time.weekday]));
            is_written = is_written && text.append(literalOf(", "));
            is_written = is_written && text.append(day.span());
            is_written = is_written && text.append(literalOf(" "));
            is_written = is_written && text.append(literalOf(MONTHS[time.month - 1]));
            is_written = is_written && text.append(literalOf(" "));
            return is_written && text.append(year.span());
        }

        bool appendClock(PrefixText& text, const CalendarTime& time)
        {
            const DigitText hour = paddedOf(time.hour);
            const DigitText minute = paddedOf(time.minute);
            const DigitText second = paddedOf(time.second);
            bool is_written = text.append(literalOf(" "));
            is_written = is_written && text.append(hour.span());
            is_written = is_written && text.append(literalOf(":"));
            is_written = is_written && text.append(minute.span());
            is_written = is_written && text.append(literalOf(":"));
            return is_written && text.append(second.span());
        }

        uint32_t withImplied(uint32_t methods)
        {
            const uint32_t get = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_GET);
            const uint32_t head = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_HEAD);
            const uint32_t options = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_OPTIONS);
            const bool has_get = (methods & get) != 0;
            const uint32_t with_head = has_get ? methods | head : methods;
            return with_head | options;
        }
    }
}
