#include "core/kiotty_parse_integer.h"

#include "core/kiotty_checked.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    namespace
    {
        struct Radix
        {
            uint64_t base;
            size_t   max_digits;
        };

        constexpr Radix    DECIMAL{ 10, 20 };
        constexpr Radix    HEXADECIMAL{ 16, 16 };
        constexpr uint64_t NOT_A_DIGIT = 0xFF;
        constexpr uint64_t FIRST_LETTER_DIGIT = 10;

        UnsignedResult parseDigits(Span<const char> text, Radix radix);
        UnsignedResult appendDigit(uint64_t value, uint64_t digit, uint64_t base);
        uint64_t       digitOf(char character);
    }

    UnsignedResult parseUnsigned(Span<const char> text)
    {
        return parseDigits(text, DECIMAL);
    }

    UnsignedResult parseHexadecimal(Span<const char> text)
    {
        return parseDigits(text, HEXADECIMAL);
    }

    namespace
    {
        UnsignedResult parseDigits(Span<const char> text, Radix radix)
        {
            const bool is_sized = text.length > 0 && text.length <= radix.max_digits;
            if (!is_sized) { return UnsignedResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            uint64_t value = 0;
            for (size_t i = 0; i < text.length; ++i)
            {
                const UnsignedResult next = appendDigit(value, digitOf(text[i]), radix.base);
                if (!next) { return next; }
                value = next.value;
            }
            return UnsignedResult::success(value);
        }

        UnsignedResult appendDigit(uint64_t value, uint64_t digit, uint64_t base)
        {
            if (digit >= base) { return UnsignedResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            const UnsignedResult shifted = checkedMultiply<uint64_t>(value, base);
            if (!shifted) { return shifted; }
            return checkedAdd<uint64_t>(shifted.value, digit);
        }

        uint64_t digitOf(char character)
        {
            const bool is_decimal = character >= '0' && character <= '9';
            if (is_decimal) { return static_cast<uint64_t>(character - '0'); }
            const bool is_lower = character >= 'a' && character <= 'f';
            if (is_lower) { return static_cast<uint64_t>(character - 'a') + FIRST_LETTER_DIGIT; }
            const bool is_upper = character >= 'A' && character <= 'F';
            if (is_upper) { return static_cast<uint64_t>(character - 'A') + FIRST_LETTER_DIGIT; }
            return NOT_A_DIGIT;
        }
    }
}
