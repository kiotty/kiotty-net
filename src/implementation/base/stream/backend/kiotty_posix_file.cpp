#if !defined(_WIN32)

#include "base/stream/kiotty_input_file.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#endif

#include <cerrno>
#include <cstring>
#include <ctime>

namespace kiotty
{
    namespace net
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
                    sigset_t waiting;
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

            ssize_t sendRange(int socket, int file, uint64_t offset, size_t length)
            {
#if defined(__linux__)
                off_t position = static_cast<off_t>(offset);
                return ::sendfile(socket, file, &position, length);
#else
                char buffer[COPY_BUFFER_BYTES];
                size_t want = length < COPY_BUFFER_BYTES ? length : COPY_BUFFER_BYTES;
                ssize_t loaded = ::pread(file, buffer, want, static_cast<off_t>(offset));
                if (loaded <= 0)
                {
                    return loaded;
                }
                return ::send(socket, buffer, static_cast<size_t>(loaded), 0);
#endif
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
                return KIOTTY_INVALID_ARGUMENT;
            }
            size_t length = strnlen(path, KIOTTY_MAX_PATH_LENGTH);
            if (length == 0 || length == KIOTTY_MAX_PATH_LENGTH)
            {
                return KIOTTY_INVALID_ARGUMENT;
            }
            int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
            if (descriptor < 0)
            {
                return KIOTTY_INVALID_ARGUMENT;
            }
            struct stat info;
            bool regular = ::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
            if (!regular)
            {
                ::close(descriptor);
                return KIOTTY_INVALID_ARGUMENT;
            }
            _handle = descriptor;
            _size = static_cast<uint64_t>(info.st_size);
            return KIOTTY_SUCCESS;
        }

        FileReadResult InputFile::read(uint64_t offset, char* buffer, size_t length)
        {
            if (!valid())
            {
                return FileReadResult::failure(KIOTTY_INVALID_ARGUMENT);
            }
            size_t done = 0;
            while (done < length)
            {
                off_t position = static_cast<off_t>(offset + done);
                ssize_t got = ::pread(descriptorOf(_handle), buffer + done, length - done, position);
                if (got < 0 && errno == EINTR)
                {
                    continue;
                }
                if (got < 0)
                {
                    return FileReadResult::failure(KIOTTY_INVALID_ARGUMENT);
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

        SocketCode sendFile(ActiveSocket& socket, InputFile& file, uint64_t offset, uint64_t length, const Deadline& deadline, uint64_t& sent)
        {
            sent = 0;
            if (!socket.valid() || !file.valid())
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            SigpipeBlock block;
            int out = static_cast<int>(socket.handle());
            int in = descriptorOf(file.handle());
            while (sent < length)
            {
                uint64_t left = length - sent;
                size_t piece = left < SEND_CHUNK_BYTES ? static_cast<size_t>(left) : SEND_CHUNK_BYTES;
                ssize_t moved = sendRange(out, in, offset + sent, piece);
                if (moved > 0)
                {
                    sent += static_cast<uint64_t>(moved);
                    continue;
                }
                if (moved == 0)
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    return lastSocketCode();
                }
                if (deadline.expired())
                {
                    return SocketCode::TIMEOUT;
                }
                SocketCode waited = socket.waitWritable(deadline.remaining());
                if (waited != SocketCode::SUCCESS)
                {
                    return waited;
                }
            }
            return SocketCode::SUCCESS;
        }
    }
}

#endif
