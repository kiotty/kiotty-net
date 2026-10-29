#include "base/error/kiotty_socket_error.h"

#include <gtest/gtest.h>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <cerrno>
#endif

using kiotty::net::SocketCode;
using kiotty::net::SocketErrorEntry;

TEST(SocketError, EveryOsCodeTheBackendProducesMapsToASocketCode)
{
    kiotty::Span<const SocketErrorEntry> table = kiotty::net::socketErrorTable();
    ASSERT_FALSE(table.empty());
    for (const SocketErrorEntry& entry : table)
    {
        EXPECT_EQ(kiotty::net::toSocketCode(entry.os_error), entry.code) << entry.os_error;
        EXPECT_NE(entry.code, SocketCode::UNKNOWN) << entry.os_error;
        EXPECT_NE(entry.code, SocketCode::SUCCESS) << entry.os_error;
    }
#if defined(_WIN32)
    EXPECT_EQ(kiotty::net::toSocketCode(WSAECONNRESET), SocketCode::RESET);
    EXPECT_EQ(kiotty::net::toSocketCode(WSAEADDRINUSE), SocketCode::ADDRESS_IN_USE);
    EXPECT_EQ(kiotty::net::toSocketCode(WSAECONNREFUSED), SocketCode::REFUSED);
    EXPECT_EQ(kiotty::net::toSocketCode(WSAEWOULDBLOCK), SocketCode::WOULD_BLOCK);
#else
    EXPECT_EQ(kiotty::net::toSocketCode(ECONNRESET), SocketCode::RESET);
    EXPECT_EQ(kiotty::net::toSocketCode(EADDRINUSE), SocketCode::ADDRESS_IN_USE);
    EXPECT_EQ(kiotty::net::toSocketCode(ECONNREFUSED), SocketCode::REFUSED);
    EXPECT_EQ(kiotty::net::toSocketCode(EAGAIN), SocketCode::WOULD_BLOCK);
#endif
    EXPECT_EQ(kiotty::net::toSocketCode(0), SocketCode::SUCCESS);
    EXPECT_EQ(kiotty::net::toSocketCode(0x7FFF1234), SocketCode::UNKNOWN);
}

TEST(SocketError, ToNetCodeMapsClosedToPeerClosedAndTheRestToSocketError)
{
    EXPECT_EQ(kiotty::net::toNetCode(SocketCode::SUCCESS), KIOTTY_SUCCESS);
    EXPECT_EQ(kiotty::net::toNetCode(SocketCode::CLOSED), KIOTTY_PEER_CLOSED);
    EXPECT_EQ(kiotty::net::toNetCode(SocketCode::CANCELLED), KIOTTY_CLOSED);
    EXPECT_EQ(kiotty::net::toNetCode(SocketCode::TIMEOUT), KIOTTY_TIMEOUT);
    SocketCode rest[] =
    {
        SocketCode::WOULD_BLOCK,
        SocketCode::PENDING,
        SocketCode::REFUSED,
        SocketCode::RESET,
        SocketCode::UNREACHABLE,
        SocketCode::ADDRESS_IN_USE,
        SocketCode::INVALID_ARGUMENT,
        SocketCode::OUT_OF_RESOURCE,
        SocketCode::NOT_SUPPORTED,
        SocketCode::UNKNOWN
    };
    for (SocketCode code : rest)
    {
        EXPECT_EQ(kiotty::net::toNetCode(code), KIOTTY_SOCKET_ERROR) << static_cast<int>(code);
    }
}
