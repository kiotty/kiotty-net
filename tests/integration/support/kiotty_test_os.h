#if !defined(KIOTTY_TEST_OS_H)
#define KIOTTY_TEST_OS_H

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <cstdint>

namespace kiotty_test
{
#if defined(_WIN32)
    typedef SOCKET RawSocket;
    static const RawSocket INVALID_RAW_SOCKET = INVALID_SOCKET;
#else
    typedef int RawSocket;
    static const RawSocket INVALID_RAW_SOCKET = -1;
#endif

    RawSocket toRaw(intptr_t handle);
    void      closeRaw(RawSocket socket);
    bool      waitReadable(RawSocket socket, uint32_t timeout_ms);
    bool      noDelayOf(intptr_t handle);
    bool      removeFile(const char* path);
    bool      createFile(const char* path);
}

#endif
