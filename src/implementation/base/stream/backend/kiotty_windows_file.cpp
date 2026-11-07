#if defined(_WIN32)

#include "base/stream/kiotty_input_file.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        const intptr_t NO_FILE = -1;
        const DWORD    READ_CHUNK_BYTES = 1u << 30;
        const DWORD    TRANSMIT_CHUNK_BYTES = 0x7FFFFFFEu;

        HANDLE fileOf(intptr_t handle)
        {
            return reinterpret_cast<HANDLE>(handle);
        }

        class ManualEvent
        {
        public:
            ManualEvent()
                : _event(::CreateEventW(nullptr, TRUE, FALSE, nullptr))
            {
            }

            ~ManualEvent()
            {
                if (_event != nullptr)
                {
                    ::CloseHandle(_event);
                }
            }

            ManualEvent(const ManualEvent&) = delete;
            ManualEvent& operator=(const ManualEvent&) = delete;

            HANDLE handle() const
            {
                return _event;
            }

            HANDLE withoutPort() const
            {
                return reinterpret_cast<HANDLE>(reinterpret_cast<uintptr_t>(_event) | 1u);
            }

        private:
            HANDLE _event;
        };

        LPFN_TRANSMITFILE transmitFileOf(SOCKET socket)
        {
            GUID id = WSAID_TRANSMITFILE;
            LPFN_TRANSMITFILE function = nullptr;
            DWORD returned = 0;
            int loaded = ::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &function, sizeof(function), &returned, nullptr, nullptr);
            if (loaded != 0)
            {
                return nullptr;
            }
            return function;
        }

        DWORD waitMsOf(const Deadline& deadline)
        {
            if (!deadline.limited())
            {
                return INFINITE;
            }
            return deadline.remaining();
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
        SOCKET out = static_cast<SOCKET>(socket.handle());
        LPFN_TRANSMITFILE transmit = transmitFileOf(out);
        ManualEvent done;
        if (transmit == nullptr || done.handle() == nullptr)
        {
            return SocketCode::OUT_OF_RESOURCE;
        }
        while (sent < length)
        {
            uint64_t left = length - sent;
            DWORD piece = left < TRANSMIT_CHUNK_BYTES ? static_cast<DWORD>(left) : TRANSMIT_CHUNK_BYTES;
            uint64_t position = offset + sent;
            OVERLAPPED request;
            std::memset(&request, 0, sizeof(request));
            request.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
            request.OffsetHigh = static_cast<DWORD>(position >> 32);
            request.hEvent = done.withoutPort();
            ::ResetEvent(done.handle());
            BOOL started = transmit(out, fileOf(file.handle()), piece, 0, &request, nullptr, 0);
            if (started == FALSE)
            {
                int error = ::WSAGetLastError();
                if (error != WSA_IO_PENDING)
                {
                    return toSocketCode(error);
                }
            }
            DWORD waited = ::WaitForSingleObject(done.handle(), waitMsOf(deadline));
            bool timed_out = waited == WAIT_TIMEOUT;
            if (timed_out)
            {
                ::CancelIoEx(reinterpret_cast<HANDLE>(out), &request);
                ::WaitForSingleObject(done.handle(), INFINITE);
            }
            DWORD moved = 0;
            DWORD flags = 0;
            BOOL finished = ::WSAGetOverlappedResult(out, &request, &moved, FALSE, &flags);
            sent += moved;
            if (timed_out)
            {
                return SocketCode::TIMEOUT;
            }
            if (finished == FALSE)
            {
                return lastSocketCode();
            }
            if (moved == 0)
            {
                return SocketCode::INVALID_ARGUMENT;
            }
        }
        return SocketCode::SUCCESS;
    }
}

#endif
