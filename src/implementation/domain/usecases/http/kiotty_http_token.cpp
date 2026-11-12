#include "domain/usecases/http/kiotty_http_token.h"

#include "core/kiotty_text.h"

#include <cstring>
#include <string>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char TOKEN_SYMBOLS[] = "!#$%&'*+-.^_`|~";
        constexpr char FIRST_VISIBLE = '!';
        constexpr char LAST_VISIBLE = '~';

        bool isAlphanumeric(char character);
    }

    bool isTokenCharacter(char character)
    {
        if (isAlphanumeric(character)) { return true; }
        if (character == '\0') { return false; }
        const char* found = std::strchr(TOKEN_SYMBOLS, character);
        return found != nullptr;
    }

    bool isVisibleCharacter(char character)
    {
        return character >= FIRST_VISIBLE && character <= LAST_VISIBLE;
    }

    bool isWhitespace(char character)
    {
        return character == ' ' || character == '\t';
    }

    bool isToken(Span<const char> text)
    {
        if (text.empty()) { return false; }
        const size_t end = skipToken(text, 0);
        return end == text.length;
    }

    size_t skipToken(Span<const char> text, size_t position)
    {
        size_t cursor = position;
        while (cursor < text.length && isTokenCharacter(text[cursor]))
        {
            ++cursor;
        }
        return cursor;
    }

    size_t skipWhitespace(Span<const char> text, size_t position)
    {
        size_t cursor = position;
        while (cursor < text.length && isWhitespace(text[cursor]))
        {
            ++cursor;
        }
        return cursor;
    }

    size_t findCharacter(Span<const char> text, char character, size_t position)
    {
        size_t cursor = position;
        while (cursor < text.length && text[cursor] != character)
        {
            ++cursor;
        }
        return cursor;
    }

    Span<const char> trimWhitespace(Span<const char> text)
    {
        const size_t start = skipWhitespace(text, 0);
        size_t end = text.length;
        while (end > start && isWhitespace(text[end - 1]))
        {
            --end;
        }
        return Span<const char>{ text.data + start, end - start };
    }

    Span<const char> spanOf(KiottyNetSpan span)
    {
        return Span<const char>{ span.data, span.length };
    }

    bool equalsLiteralIgnoreCase(Span<const char> text, const char* literal)
    {
        const Span<const char> expected{ literal, std::char_traits<char>::length(literal) };
        return equalsIgnoreCase(text, expected);
    }

    namespace
    {
        bool isAlphanumeric(char character)
        {
            const bool is_digit = character >= '0' && character <= '9';
            const bool is_lower = character >= 'a' && character <= 'z';
            const bool is_upper = character >= 'A' && character <= 'Z';
            return is_digit || is_lower || is_upper;
        }
    }
}
