#include "core/kiotty_base64.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char     ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz0123456789+/";
        constexpr char     PAD = '=';
        constexpr size_t   GROUP_BYTES = 3;
        constexpr size_t   GROUP_CHARACTERS = 4;
        constexpr size_t   MAX_PADDING = 2;
        constexpr uint32_t BITS_PER_BYTE = 8;
        constexpr uint32_t BITS_PER_CHARACTER = 6;
        constexpr uint32_t CHARACTER_MASK = 0x3F;

        void encodeGroup(Span<const uint8_t> group, Span<char> target);
        bool isAlphabetCharacter(char character);
    }

    size_t base64LengthOf(size_t length)
    {
        return (length + GROUP_BYTES - 1) / GROUP_BYTES * GROUP_CHARACTERS;
    }

    EncodedResult encodeBase64(Span<const uint8_t> input, Span<char> target)
    {
        const size_t length = base64LengthOf(input.length);
        if (length > target.length) { return EncodedResult::failure(KIOTTY_NET_CODE_BUFFER_FULL); }
        for (size_t i = 0; i < input.length; i += GROUP_BYTES)
        {
            const Span<const uint8_t> rest = input.skip(i);
            const size_t taken = rest.length < GROUP_BYTES ? rest.length : GROUP_BYTES;
            const Span<const uint8_t> group{ rest.data, taken };
            encodeGroup(group, target.skip(i / GROUP_BYTES * GROUP_CHARACTERS));
        }
        return EncodedResult::success(length);
    }

    bool isBase64Text(Span<const char> text)
    {
        if (text.length % GROUP_CHARACTERS != 0) { return false; }
        size_t padding = 0;
        for (size_t i = 0; i < text.length; ++i)
        {
            const bool is_pad = text[i] == PAD;
            const bool is_after_pad = !is_pad && padding > 0;
            const bool is_known = is_pad || isAlphabetCharacter(text[i]);
            if (is_after_pad || !is_known) { return false; }
            padding += is_pad ? 1 : 0;
        }
        return padding <= MAX_PADDING;
    }

    namespace
    {
        void encodeGroup(Span<const uint8_t> group, Span<char> target)
        {
            uint32_t bits = 0;
            for (size_t i = 0; i < GROUP_BYTES; ++i)
            {
                const uint32_t byte = i < group.length ? group[i] : 0u;
                bits = (bits << BITS_PER_BYTE) | byte;
            }
            for (size_t i = 0; i < GROUP_CHARACTERS; ++i)
            {
                const uint32_t place = static_cast<uint32_t>(GROUP_CHARACTERS - 1 - i);
                const uint32_t shift = place * BITS_PER_CHARACTER;
                const bool is_padding = i > group.length;
                target[i] = is_padding ? PAD : ALPHABET[(bits >> shift) & CHARACTER_MASK];
            }
        }

        bool isAlphabetCharacter(char character)
        {
            const bool is_upper = character >= 'A' && character <= 'Z';
            const bool is_lower = character >= 'a' && character <= 'z';
            const bool is_digit = character >= '0' && character <= '9';
            return is_upper || is_lower || is_digit || character == '+' || character == '/';
        }
    }
}
