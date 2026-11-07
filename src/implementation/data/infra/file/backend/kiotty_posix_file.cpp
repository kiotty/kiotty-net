#if !defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/file/kiotty_input_file.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>

namespace kiotty::net::implementation
{
    namespace
    {
        const intptr_t NO_FILE = -1;

        const size_t SEND_CHUNK_BYTES = 1u << 30;
#if !defined(__linux__)
        const size_t COPY_BUFFER_BYTES = 4096;
#endif

        int descriptorOf(intptr_t handle)
        {
            return static_cast<int>(handle);
        }

#if defined(__linux__)
        class SigpipeBlock
        {
        public:
            SigpipeBlock()
                : _blocked()
                , _previous()
                , _pending(false)
            {
                sigemptyset(&_blocked);
                sigaddset(&_blocked, SIGPIPE);
                sigset_t waiting{};
                sigemptyset(&waiting);
                sigpending(&waiting);
                _pending = sigismember(&waiting, SIGPIPE) == 1;
                pthread_sigmask(SIG_BLOCK, &_blocked, &_previous);
            }

            ~SigpipeBlock()
            {
                if (!_pending)
                {
                    timespec now = { 0, 0 };
                    sigtimedwait(&_blocked, nullptr, &now);
                }
                pthread_sigmask(SIG_SETMASK, &_previous, nullptr);
            }

            SigpipeBlock(const SigpipeBlock&) = delete;
            SigpipeBlock& operator=(const SigpipeBlock&) = delete;

        private:
            sigset_t _blocked;
            sigset_t _previous;
            bool     _pending;
        };
#endif

        ssize_t sendRange(int socket, int file, uint64_t offset, size_t length)
        {
#if defined(__linux__)
            off_t position = static_cast<off_t>(offset);
            return ::sendfile(socket, file, &position, length);
#else
            char buffer[COPY_BUFFER_BYTES] = {};
            const size_t want = std::min(length, COPY_BUFFER_BYTES);
            const ssize_t loaded =::pread(file, buffer, want, static_cast<off_t>(offset));
            if (loaded <= 0)
            {
                return loaded;
            }
            return ::send(socket, buffer, static_cast<size_t>(loaded), 0);
#endif
        }

        Result<uint64_t, SocketCode> sendPiece(int out, int in, uint64_t position, uint64_t left)
        {
            using Moved = Result<uint64_t, SocketCode>;
            const uint64_t capped = std::min<uint64_t>(left, SEND_CHUNK_BYTES);
            const size_t piece = static_cast<size_t>(capped);
            const ssize_t moved = sendRange(out, in, position, piece);
            if (moved > 0) { return Moved::success(static_cast<uint64_t>(moved)); }
            if (moved == 0) { return Moved::failure(SocketCode::INVALID_ARGUMENT); }
            if (errno == EINTR) { return Moved::success(0); }
            const bool is_would_block = errno == EAGAIN || errno == EWOULDBLOCK;
            if (!is_would_block) { return Moved::failure(lastSocketCode()); }
            return Moved::failure(SocketCode::WOULD_BLOCK);
        }

        SocketCode awaitWritable(const ActiveSocket& socket, const Deadline& deadline)
        {
            if (deadline.expired()) { return SocketCode::TIMEOUT; }
            return implementation::waitWritable(socket, deadline.remaining());
        }
    }

    InputFile::InputFile()
        : _handle(NO_FILE)
        , _size(0)
    {
    }

    InputFile::~InputFile()
    {
        close();
    }

    KiottyNetCode InputFile::open(const char* path)
    {
        close();
        if (path == nullptr)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        const size_t length = strnlen(path, KIOTTY_NET_MAX_PATH_LENGTH);
        if (length == 0 || length == KIOTTY_NET_MAX_PATH_LENGTH)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
        if (descriptor < 0)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        struct stat info{};
        const bool regular = ::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
        if (!regular)
        {
            ::close(descriptor);
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        _handle = descriptor;
        _size = static_cast<uint64_t>(info.st_size);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    FileReadResult InputFile::read(uint64_t offset, Span<char> target)
    {
        if (!valid())
        {
            return FileReadResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        size_t done = 0;
        while (done < target.length)
        {
            const off_t position = static_cast<off_t>(offset + done);
            const ssize_t got =
                ::pread(descriptorOf(_handle), target.data + done, target.length - done, position);
            if (got < 0 && errno == EINTR)
            {
                continue;
            }
            if (got < 0)
            {
                return FileReadResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            if (got == 0)
            {
                break;
            }
            done += static_cast<size_t>(got);
        }
        return FileReadResult::success(done);
    }

    void InputFile::close()
    {
        if (!valid())
        {
            return;
        }
        ::close(descriptorOf(_handle));
        _handle = NO_FILE;
        _size = 0;
    }

    bool InputFile::valid() const
    {
        return _handle != NO_FILE;
    }

    uint64_t InputFile::size() const
    {
        return _size;
    }

    intptr_t InputFile::handle() const
    {
        return _handle;
    }

    SocketCode sendFile(const ActiveSocket& socket, const InputFile& file, FileRange range,
        const Deadline& deadline)
    {
        if (!socket.valid() || !file.valid()) { return SocketCode::INVALID_ARGUMENT; }
#if defined(__linux__)
        const SigpipeBlock block;
#endif
        const int out = static_cast<int>(socket.handle());
        const int in = descriptorOf(file.handle());
        uint64_t sent = 0;
        while (sent < range.length)
        {
            const uint64_t position = range.offset + sent;
            const uint64_t left = range.length - sent;
            const Result<uint64_t, SocketCode> moved = sendPiece(out, in, position, left);
            if (moved)
            {
                sent += moved.value;
                continue;
            }
            if (moved.code != SocketCode::WOULD_BLOCK) { return moved.code; }
            const SocketCode waited = awaitWritable(socket, deadline);
            if (waited != SocketCode::SUCCESS) { return waited; }
        }
        return SocketCode::SUCCESS;
    }
}

#endif
