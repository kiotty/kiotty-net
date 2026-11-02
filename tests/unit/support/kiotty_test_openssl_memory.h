#if !defined(KIOTTY_TEST_OPENSSL_MEMORY_H)
#define KIOTTY_TEST_OPENSSL_MEMORY_H

#include <cstddef>

namespace kiotty_test
{
    struct OpenSslMemory
    {
        bool   counting;
        size_t live_bytes;
        size_t live_blocks;
        size_t peak_bytes;
    };

    OpenSslMemory openSslMemory();
    void          resetOpenSslPeak();
}

#endif
