#if !defined(KIOTTY_IO_ACTION_H)
#define KIOTTY_IO_ACTION_H

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        enum class IoActionKind : uint8_t
        {
            NONE,
            HOLD,
            READ,
            READ_UNTIL,
            WRITE,
            START_WORK,
            CLOSE,
            ACCEPT
        };

        enum class IoPurpose : uint8_t
        {
            USER,
            HANDSHAKE,
            CONTROL,
            NOTIFY
        };

        struct IoAction
        {
            static const size_t MAX_DELIMITER_LENGTH = 4;

            IoActionKind kind;
            IoPurpose    purpose;
            uint32_t     timeout_ms;
            union
            {
                struct
                {
                    size_t exact;
                } read;
                struct
                {
                    char    delimiter[MAX_DELIMITER_LENGTH];
                    uint8_t length;
                    size_t  max;
                    size_t  scanned;
                } read_until;
                struct
                {
                    KiottyNetCode reason;
                } close;
            } as;

            static IoAction none();
            static IoAction hold(uint32_t timeout_ms);
            static IoAction read(IoPurpose purpose, uint32_t timeout_ms, size_t exact);
            static IoAction readUntil(uint32_t timeout_ms, KiottyNetSpan delimiter, size_t max);
            static IoAction write(IoPurpose purpose, uint32_t timeout_ms);
            static IoAction startWork();
            static IoAction close(KiottyNetCode reason);
            static IoAction accept();
            static IoAction from(const KiottyNetConnectionAction& action);
        };
    }
}

#endif
