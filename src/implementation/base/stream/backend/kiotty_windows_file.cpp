#if defined(_WIN32)

#include "base/stream/kiotty_input_file.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <windows.h>

#include <cstring>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const intptr_t NO_FILE = -1;
            const DWORD    READ_CHUNK_BYTES = 1u << 30;

            HANDLE fileOf(intptr_t handle)
            {
                return reinterpret_cast<HANDLE>(handle);
            }

            bool widen(const char* path, wchar_t (&wide)[KIOTTY_MAX_PATH_LENGTH])
            {
                int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, KIOTTY_MAX_PATH_LENGTH);
                return written > 0;
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
            wchar_t wide[KIOTTY_MAX_PATH_LENGTH];
            if (!widen(path, wide))
            {
                return KIOTTY_INVALID_ARGUMENT;
            }
            HANDLE file = ::CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return KIOTTY_INVALID_ARGUMENT;
            }
            LARGE_INTEGER size;
            if (::GetFileSizeEx(file, &size) == FALSE)
            {
                ::CloseHandle(file);
                return KIOTTY_INVALID_ARGUMENT;
            }
            _handle = reinterpret_cast<intptr_t>(file);
            _size = static_cast<uint64_t>(size.QuadPart);
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
                uint64_t position = offset + done;
                OVERLAPPED at;
                std::memset(&at, 0, sizeof(at));
                at.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
                at.OffsetHigh = static_cast<DWORD>(position >> 32);
                size_t left = length - done;
                DWORD request = left < READ_CHUNK_BYTES ? static_cast<DWORD>(left) : READ_CHUNK_BYTES;
                DWORD got = 0;
                BOOL completed = ::ReadFile(fileOf(_handle), buffer + done, request, &got, &at);
                if (completed == FALSE && ::GetLastError() != ERROR_HANDLE_EOF)
                {
                    return FileReadResult::failure(KIOTTY_INVALID_ARGUMENT);
                }
                if (got == 0)
                {
                    break;
                }
                done += got;
            }
            return FileReadResult::success(done);
        }

        void InputFile::close()
        {
            if (!valid())
            {
                return;
            }
            ::CloseHandle(fileOf(_handle));
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
