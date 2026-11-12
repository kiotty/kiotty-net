#include "core/kiotty_sha1.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   BLOCK_BYTES = 64;
        constexpr size_t   LENGTH_BYTES = 8;
        constexpr size_t   ROUNDS = 80;
        constexpr size_t   ROUNDS_PER_STAGE = 20;
        constexpr size_t   BLOCK_WORDS = 16;
        constexpr size_t   STATE_WORDS = 5;
        constexpr size_t   WORD_BYTES = 4;
        constexpr uint8_t  END_MARK = 0x80;
        constexpr uint32_t BITS_PER_BYTE = 8;
        constexpr uint32_t WORD_BITS = 32;
        constexpr uint32_t INITIAL[STATE_WORDS] = {
            0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u,
        };
        constexpr uint32_t STAGE_CONSTANTS[ROUNDS / ROUNDS_PER_STAGE] = {
            0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u,
        };

        struct Sha1State
        {
            uint32_t words[STATE_WORDS];
            uint8_t  block[BLOCK_BYTES];
            size_t   filled;
            uint64_t total;
        };

        uint32_t rotateLeft(uint32_t value, uint32_t count);
        uint32_t mixOf(size_t round, uint32_t b, uint32_t c, uint32_t d);
        uint32_t wordAt(const uint8_t (&block)[BLOCK_BYTES], size_t index);
        void     compress(Sha1State& state);
        void     absorb(Sha1State& state, Span<const char> data);
        void     absorbByte(Sha1State& state, uint8_t byte);
        void     finish(Sha1State& state);
    }

    Sha1Digest sha1Of(Span<const char> first, Span<const char> second)
    {
        Sha1State state{};
        for (size_t i = 0; i < STATE_WORDS; ++i) { state.words[i] = INITIAL[i]; }
        absorb(state, first);
        absorb(state, second);
        finish(state);
        Sha1Digest digest{};
        for (size_t i = 0; i < SHA1_DIGEST_BYTES; ++i)
        {
            const uint32_t byte_in_word = static_cast<uint32_t>(WORD_BYTES - 1 - i % WORD_BYTES);
            const uint32_t word = state.words[i / WORD_BYTES];
            digest.bytes[i] = static_cast<uint8_t>(word >> (byte_in_word * BITS_PER_BYTE));
        }
        return digest;
    }

    namespace
    {
        uint32_t rotateLeft(uint32_t value, uint32_t count)
        {
            return (value << count) | (value >> (WORD_BITS - count));
        }

        uint32_t mixOf(size_t round, uint32_t b, uint32_t c, uint32_t d)
        {
            const size_t stage = round / ROUNDS_PER_STAGE;
            if (stage == 0) { return (b & c) | (~b & d); }
            if (stage == 2) { return (b & c) | (b & d) | (c & d); }
            return b ^ c ^ d;
        }

        uint32_t wordAt(const uint8_t (&block)[BLOCK_BYTES], size_t index)
        {
            uint32_t word = 0;
            for (size_t i = 0; i < WORD_BYTES; ++i)
            {
                word = (word << BITS_PER_BYTE) | block[index * WORD_BYTES + i];
            }
            return word;
        }

        void compress(Sha1State& state)
        {
            uint32_t schedule[ROUNDS] = {};
            for (size_t i = 0; i < BLOCK_WORDS; ++i) { schedule[i] = wordAt(state.block, i); }
            for (size_t i = BLOCK_WORDS; i < ROUNDS; ++i)
            {
                const uint32_t near = schedule[i - 3] ^ schedule[i - 8];
                const uint32_t far = schedule[i - 14] ^ schedule[i - 16];
                const uint32_t mixed = near ^ far;
                schedule[i] = rotateLeft(mixed, 1);
            }
            uint32_t v[STATE_WORDS] = {};
            for (size_t i = 0; i < STATE_WORDS; ++i) { v[i] = state.words[i]; }
            for (size_t round = 0; round < ROUNDS; ++round)
            {
                const uint32_t constant = STAGE_CONSTANTS[round / ROUNDS_PER_STAGE];
                const uint32_t mixed = mixOf(round, v[1], v[2], v[3]);
                const uint32_t rotated = rotateLeft(v[0], 5);
                const uint32_t next = rotated + mixed + v[4] + constant + schedule[round];
                v[4] = v[3];
                v[3] = v[2];
                v[2] = rotateLeft(v[1], 30);
                v[1] = v[0];
                v[0] = next;
            }
            for (size_t i = 0; i < STATE_WORDS; ++i) { state.words[i] += v[i]; }
            state.filled = 0;
        }

        void absorbByte(Sha1State& state, uint8_t byte)
        {
            state.block[state.filled] = byte;
            state.filled += 1;
            if (state.filled == BLOCK_BYTES) { compress(state); }
        }

        void absorb(Sha1State& state, Span<const char> data)
        {
            for (size_t i = 0; i < data.length; ++i)
            {
                absorbByte(state, static_cast<uint8_t>(data[i]));
            }
            state.total += data.length;
        }

        void finish(Sha1State& state)
        {
            const uint64_t bits = state.total * BITS_PER_BYTE;
            absorbByte(state, END_MARK);
            while (state.filled != BLOCK_BYTES - LENGTH_BYTES) { absorbByte(state, 0); }
            for (size_t i = 0; i < LENGTH_BYTES; ++i)
            {
                const uint64_t shift = (LENGTH_BYTES - 1 - i) * BITS_PER_BYTE;
                absorbByte(state, static_cast<uint8_t>(bits >> shift));
            }
        }
    }
}
