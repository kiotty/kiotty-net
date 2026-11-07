#include "data/infra/error/kiotty_socket_error.h"

#include <gtest/gtest.h>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <cerrno>
#endif

using kiotty::net::implementation::SocketCode;
using kiotty::net::implementation::SocketErrorEntry;

TEST(SocketError, EveryOsCodeTheBackendProducesMapsToASocketCode)
{
    kiotty::net::implementation::Span<const SocketErrorEntry> table = kiotty::net::implementation::socketErrorTable();
    ASSERT_FALSE(table.empty());
    for (const SocketErrorEntry& entry : table)
    {
        EXPECT_EQ(kiotty::net::implementation::toSocketCode(entry.os_error), entry.code) << entry.os_error;
        EXPECT_NE(entry.code, SocketCode::UNKNOWN) << entry.os_error;
        EXPECT_NE(entry.code, SocketCode::SUCCESS) << entry.os_error;
    }
#if defined(_WIN32)
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(WSAECONNRESET), SocketCode::RESET);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(WSAEADDRINUSE), SocketCode::ADDRESS_IN_USE);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(WSAECONNREFUSED), SocketCode::REFUSED);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(WSAEWOULDBLOCK), SocketCode::WOULD_BLOCK);
#else
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(ECONNRESET), SocketCode::RESET);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(EADDRINUSE), SocketCode::ADDRESS_IN_USE);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(ECONNREFUSED), SocketCode::REFUSED);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(EAGAIN), SocketCode::WOULD_BLOCK);
#endif
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(0), SocketCode::SUCCESS);
    EXPECT_EQ(kiotty::net::implementation::toSocketCode(0x7FFF1234), SocketCode::UNKNOWN);
}

TEST(SocketError, ToNetCodeMapsClosedToPeerClosedBusyToBusyAndTheRestToSocketError)
{
    EXPECT_EQ(kiotty::net::implementation::toNetCode(SocketCode::SUCCESS), KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(kiotty::net::implementation::toNetCode(SocketCode::CLOSED), KIOTTY_NET_CODE_PEER_CLOSED);
    EXPECT_EQ(kiotty::net::implementation::toNetCode(SocketCode::CANCELLED), KIOTTY_NET_CODE_CLOSED);
    EXPECT_EQ(kiotty::net::implementation::toNetCode(SocketCode::TIMEOUT), KIOTTY_NET_CODE_TIMEOUT);
    EXPECT_EQ(kiotty::net::implementation::toNetCode(SocketCode::BUSY), KIOTTY_NET_CODE_BUSY);
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
        EXPECT_EQ(kiotty::net::implementation::toNetCode(code), KIOTTY_NET_CODE_SOCKET_ERROR) << static_cast<int>(code);
    }
}
