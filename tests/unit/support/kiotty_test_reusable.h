#if !defined(KIOTTY_TEST_REUSABLE_H)
#define KIOTTY_TEST_REUSABLE_H

#include "core/pool/kiotty_pool_handle.h"

namespace kiotty_test
{
    struct ReusableCounts
    {
        int constructed;
        int destroyed;
        int initialized;
        int resets;
    };

    inline ReusableCounts& reusableCounts()
    {
        static ReusableCounts counts = { 0, 0, 0, 0 };
        return counts;
    }

    inline void resetReusableCounts()
    {
        ReusableCounts zero = { 0, 0, 0, 0 };
        reusableCounts() = zero;
    }

    struct ReusableConfiguration
    {
        int value;
        int fail_with;
    };

    class Reusable
    {
    public:
        typedef int                   code_type;
        typedef ReusableConfiguration configuration_type;

        enum : int
        {
            TABLE_FULL = 10,
            OUT_OF_MEMORY = 11,
            INVALID_HANDLE = 12
        };

        Reusable()
            : _id()
            , _value(0)
            , _active(false)
        {
            reusableCounts().constructed += 1;
        }

        ~Reusable()
        {
            reusableCounts().destroyed += 1;
        }

        Reusable(const Reusable&) = delete;
        Reusable& operator=(const Reusable&) = delete;

        int initialize(kiotty::net::implementation::PoolHandle id, ReusableConfiguration configuration)
        {
            reusableCounts().initialized += 1;
            _id = id;
            _value = configuration.value;
            _active = configuration.fail_with == 0;
            return configuration.fail_with;
        }

        void reset() noexcept
        {
            reusableCounts().resets += 1;
            _value = 0;
            _active = false;
        }

        kiotty::net::implementation::PoolHandle id() const
        {
            return _id;
        }

        int value() const
        {
            return _value;
        }

        bool active() const
        {
            return _active;
        }

    private:
        kiotty::net::implementation::PoolHandle _id;
        int                _value;
        bool               _active;
    };

    inline ReusableConfiguration succeedWith(int value)
    {
        ReusableConfiguration configuration = { value, 0 };
        return configuration;
    }

    inline ReusableConfiguration failWith(int code)
    {
        ReusableConfiguration configuration = { 0, code };
        return configuration;
    }
}

#endif
