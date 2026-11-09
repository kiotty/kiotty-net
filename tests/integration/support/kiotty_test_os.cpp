#include "support/kiotty_test_os.h"

#include <cstdio>
#include <fstream>

namespace kiotty_test
{
    RawSocket toRaw(intptr_t handle)
    {
        return static_cast<RawSocket>(handle);
    }

    void closeRaw(RawSocket socket)
    {
        if (socket == INVALID_RAW_SOCKET)
        {
            return;
        }
#if defined(_WIN32)
        ::closesocket(socket);
#else
        ::close(socket);
#endif
    }

    bool waitReadable(RawSocket socket, uint32_t timeout_milliseconds)
    {
#if defined(_WIN32)
        WSAPOLLFD entry;
        entry.fd = socket;
        entry.events = POLLRDNORM;
        entry.revents = 0;
        int ready = ::WSAPoll(&entry, 1, static_cast<INT>(timeout_milliseconds));
#else
        pollfd entry;
        entry.fd = socket;
        entry.events = POLLIN;
        entry.revents = 0;
        int ready = ::poll(&entry, 1, static_cast<int>(timeout_milliseconds));
#endif
        return ready > 0;
    }

    bool setSendBuffer(intptr_t handle, int bytes)
    {
        RawSocket socket = toRaw(handle);
#if defined(__APPLE__)
        if (bytes == 0)
        {
            bytes = 1;
        }
#endif
        return ::setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes)) == 0;
    }

    bool shutdownBoth(intptr_t handle)
    {
        RawSocket socket = toRaw(handle);
#if defined(_WIN32)
        return ::shutdown(socket, SD_BOTH) == 0;
#else
        return ::shutdown(socket, SHUT_RDWR) == 0;
#endif
    }

    bool noDelayOf(intptr_t handle)
    {
        RawSocket socket = toRaw(handle);
#if defined(_WIN32)
        BOOL value = FALSE;
        int length = sizeof(value);
        ::getsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&value), &length);
        return value != FALSE;
#else
        int value = 0;
        socklen_t length = sizeof(value);
        ::getsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &value, &length);
        return value != 0;
#endif
    }

    bool removeFile(const char* path)
    {
        return std::remove(path) == 0;
    }

    bool createFile(const char* path)
    {
        std::ofstream file(path, std::ios::binary);
        return file.good();
    }
}
