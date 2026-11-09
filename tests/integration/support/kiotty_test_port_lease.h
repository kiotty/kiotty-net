#if !defined(KIOTTY_TEST_PORT_LEASE_H)
#define KIOTTY_TEST_PORT_LEASE_H

#include "support/kiotty_test_os.h"

#include <cstdint>

namespace kiotty_test
{
    class PortLease
    {
    public:
        PortLease();
        ~PortLease();

        PortLease(const PortLease&) = delete;
        PortLease& operator=(const PortLease&) = delete;

        bool     locked() const;
        uint32_t take();

    private:
#if defined(_WIN32)
        HANDLE _file;
#else
        int _file;
#endif
    };
}

#endif
