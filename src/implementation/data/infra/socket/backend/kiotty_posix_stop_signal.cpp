#if !defined(_WIN32)

#include "data/infra/socket/kiotty_stop_signal.h"

#include "data/infra/error/kiotty_socket_error.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t DRAIN_BYTES = 64;
        constexpr int    PIPE_ENDS = 2;

        SocketCode prepareEnd(int descriptor);
    }

    SocketCode StopSignal::open()
    {
        if (_reader != INVALID_SOCKET_HANDLE) { return SocketCode::SUCCESS; }
        int ends[PIPE_ENDS] = { -1, -1 };
        if (::pipe(ends) != 0) { return lastSocketCode(); }
        _reader = ends[0];
        _writer = ends[1];
        const SocketCode reader = prepareEnd(ends[0]);
        const SocketCode writer = prepareEnd(ends[1]);
        const bool is_prepared = reader == SocketCode::SUCCESS && writer == SocketCode::SUCCESS;
        if (is_prepared) { return SocketCode::SUCCESS; }
        close();
        return reader != SocketCode::SUCCESS ? reader : writer;
    }

    void StopSignal::drain() const
    {
        if (_reader == INVALID_SOCKET_HANDLE) { return; }
        char sink[DRAIN_BYTES]{};
        for (;;)
        {
            const ssize_t taken = ::read(static_cast<int>(_reader), sink, sizeof(sink));
            const bool is_interrupted = taken < 0 && errno == EINTR;
            if (taken <= 0 && !is_interrupted) { return; }
        }
    }

    void StopSignal::close()
    {
        if (_reader != INVALID_SOCKET_HANDLE) { ::close(static_cast<int>(_reader)); }
        if (_writer != INVALID_SOCKET_HANDLE) { ::close(static_cast<int>(_writer)); }
        _reader = INVALID_SOCKET_HANDLE;
        _writer = INVALID_SOCKET_HANDLE;
    }

    void StopSignal::notify() const
    {
        if (_writer == INVALID_SOCKET_HANDLE) { return; }
        const char wake = 1;
        for (;;)
        {
            const ssize_t written = ::write(static_cast<int>(_writer), &wake, sizeof(wake));
            if (written >= 0 || errno != EINTR) { return; }
        }
    }

    namespace
    {
        SocketCode prepareEnd(int descriptor)
        {
            const int flags = ::fcntl(descriptor, F_GETFL, 0);
            if (flags < 0) { return lastSocketCode(); }
            const int non_blocking = ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
            if (non_blocking < 0) { return lastSocketCode(); }
            const int close_on_exec = ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
            if (close_on_exec < 0) { return lastSocketCode(); }
            return SocketCode::SUCCESS;
        }
    }
}

#endif
