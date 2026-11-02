#if !defined(KIOTTY_TEST_POOL_H)
#define KIOTTY_TEST_POOL_H

#include "core/kiotty_pool.h"

namespace kiotty_test
{
    typedef kiotty::PoolLayout<
        kiotty::BlockClass<64, 256>,
        kiotty::BlockClass<1024, 256>,
        kiotty::BlockClass<4096, 256>,
        kiotty::BlockClass<16384, 256>,
        kiotty::BlockClass<32768, 128>,
        kiotty::BlockClass<81920, 64>,
        kiotty::BlockClass<131072, 32>
    > TestPoolLayout;

    class TestPool : public kiotty::Pool
    {
    public:
        TestPool()
            : kiotty::Pool(TestPoolLayout::classes(), TestPoolLayout::CLASS_COUNT)
        {
        }
    };
}

#endif
