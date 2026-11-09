#include "core/kiotty_text.h"

#include "core/kiotty_checked.h"

namespace kiotty::net::implementation
{
    namespace
    {
        struct ByteRange
        {
            unsigned char low;
            unsigned char high;
        };

        constexpr ByteRange     CONTINUATION{ 0x80, 0xBF };
        constexpr unsigned char ASCII_END = 0x80;
        constexpr char          UPPER_TO_LOWER = 'a' - 'A';

        bool          isUtf8(Span<const char> text);
        size_t        measureSequence(Span<const char> rest);
        size_t        sequenceLength(unsigned char lead);
        ByteRange     secondByteRange(unsigned char lead);
        unsigned char byteOf(char character);
        char          lowerOf(char character);
    }

    TextResult parseText(const char* text, size_t max_length)
    {
        if (text == nullptr) { return TextResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const auto scan_length = checkedAdd<size_t>(max_length, 1);
        if (!scan_length) { return TextResult::failure(scan_length.code); }
        const size_t length = strnlen(text, scan_length.value);
        if (length > max_length) { return TextResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const Span<const char> parsed{ text, length };
        if (!isUtf8(parsed)) { return TextResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return TextResult::success(parsed);
    }

    bool equalsIgnoreCase(Span<const char> left, Span<const char> right)
    {
        if (left.length != right.length) { return false; }
        for (size_t i = 0; i < left.length; ++i)
        {
            if (lowerOf(left[i]) != lowerOf(right[i])) { return false; }
        }
        return true;
    }

    namespace
    {
        bool isUtf8(Span<const char> text)
        {
            size_t position = 0;
            while (position < text.length)
            {
                const size_t length = measureSequence(text.skip(position));
                if (length == 0) { return false; }
                position += length;
            }
            return true;
        }

        size_t measureSequence(Span<const char> rest)
        {
            const unsigned char lead = byteOf(rest[0]);
            const size_t length = sequenceLength(lead);
            if (length == 0 || length > rest.length) { return 0; }
            const ByteRange second = secondByteRange(lead);
            for (size_t i = 1; i < length; ++i)
            {
                const ByteRange range = i == 1 ? second : CONTINUATION;
                const unsigned char byte = byteOf(rest[i]);
                if (byte < range.low || byte > range.high) { return 0; }
            }
            return length;
        }

        size_t sequenceLength(unsigned char lead)
        {
            if (lead < ASCII_END) { return 1; }
            if (lead >= 0xC2 && lead <= 0xDF) { return 2; }
            if (lead >= 0xE0 && lead <= 0xEF) { return 3; }
            if (lead >= 0xF0 && lead <= 0xF4) { return 4; }
            return 0;
        }

        ByteRange secondByteRange(unsigned char lead)
        {
            if (lead == 0xE0) { return ByteRange{ 0xA0, 0xBF }; }
            if (lead == 0xED) { return ByteRange{ 0x80, 0x9F }; }
            if (lead == 0xF0) { return ByteRange{ 0x90, 0xBF }; }
            if (lead == 0xF4) { return ByteRange{ 0x80, 0x8F }; }
            return CONTINUATION;
        }

        unsigned char byteOf(char character)
        {
            return static_cast<unsigned char>(character);
        }

        char lowerOf(char character)
        {
            const bool is_upper = character >= 'A' && character <= 'Z';
            if (!is_upper) { return character; }
            return static_cast<char>(character + UPPER_TO_LOWER);
        }
    }
}
