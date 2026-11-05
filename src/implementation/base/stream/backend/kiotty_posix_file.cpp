#if !defined(_WIN32)

#include "base/stream/kiotty_input_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const intptr_t NO_FILE = -1;

            int descriptorOf(intptr_t handle)
            {
                return static_cast<int>(handle);
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
    }
}

#endif
