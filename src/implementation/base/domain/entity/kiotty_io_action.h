#pragma once

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
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
        static constexpr size_t MAX_DELIMITER_LENGTH = 4;

        union
        {
            struct
            {
                char    delimiter[MAX_DELIMITER_LENGTH];
                uint8_t length;
                size_t  max;
                size_t  scanned;
            } read_until;
            struct
            {
                size_t exact;
            } read;
            struct
            {
                size_t length;
                size_t notify_length;
            } write;
            struct
            {
                KiottyNetCode reason;
                bool          is_after_send;
            } close;
        } as;
        uint32_t     timeout_milliseconds;
        IoActionKind kind;
        IoPurpose    purpose;

        static IoAction none();
        static IoAction hold(uint32_t timeout_milliseconds);
        static IoAction read(IoPurpose purpose, uint32_t timeout_milliseconds, size_t exact);
        static IoAction readUntil(uint32_t timeout_milliseconds,
                                  KiottyNetSpan delimiter,
                                  size_t max);
        static IoAction write(IoPurpose purpose, uint32_t timeout_milliseconds, size_t length);
        static IoAction gather(size_t length, size_t notify_length);
        static IoAction startWork();
        static IoAction close(KiottyNetCode reason);
        static IoAction closeAfterSend(KiottyNetCode reason);
        static IoAction accept();
        static IoAction from(const KiottyNetConnectionAction& action);
    };

    static_assert(sizeof(IoAction) == 2 * sizeof(size_t) + 16, "IoAction: unexpected padding");
}
