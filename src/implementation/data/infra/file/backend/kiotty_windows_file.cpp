#if defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/file/kiotty_input_file.h"

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

        DWORD waitMillisecondsOf(const Deadline& deadline)
        {
            if (!deadline.limited()) { return INFINITE; }
            return deadline.remaining();
        }

        struct Transmission
        {
            LPFN_TRANSMITFILE  transmit;
            const ManualEvent& done;
            HANDLE             file;
            SOCKET             out;
        };

        using Transmitted = Result<DWORD, SocketCode>;

        Transmitted finishPiece(const Transmission& transmission, OVERLAPPED& request, const Deadline& deadline)
        {
            const DWORD waited = ::WaitForSingleObject(transmission.done.handle(), waitMillisecondsOf(deadline));
            const bool timed_out = waited == WAIT_TIMEOUT;
            if (timed_out)
            {
                ::CancelIoEx(reinterpret_cast<HANDLE>(transmission.out), &request);
                ::WaitForSingleObject(transmission.done.handle(), INFINITE);
            }
            DWORD moved = 0;
            DWORD flags = 0;
            const BOOL finished = ::WSAGetOverlappedResult(transmission.out, &request, &moved, FALSE, &flags);
            if (timed_out) { return Transmitted::failure(SocketCode::TIMEOUT); }
            if (finished == FALSE) { return Transmitted::failure(lastSocketCode()); }
            if (moved == 0) { return Transmitted::failure(SocketCode::INVALID_ARGUMENT); }
            return Transmitted::success(moved);
        }

        Transmitted transmitPiece(const Transmission& transmission, uint64_t position, DWORD piece, const Deadline& deadline)
        {
            OVERLAPPED request{};
            request.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
            request.OffsetHigh = static_cast<DWORD>(position >> 32);
            request.hEvent = transmission.done.withoutPort();
            ::ResetEvent(transmission.done.handle());
            const BOOL started = transmission.transmit(transmission.out, transmission.file, piece, 0, &request, nullptr, 0);
            if (started == FALSE)
            {
                const int error = ::WSAGetLastError();
                if (error != WSA_IO_PENDING) { return Transmitted::failure(toSocketCode(error)); }
            }
            return finishPiece(transmission, request, deadline);
        }

        bool widen(const char* path, wchar_t (&wide)[KIOTTY_NET_MAX_PATH_LENGTH])
        {
            int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, KIOTTY_NET_MAX_PATH_LENGTH);
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
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        size_t length = strnlen(path, KIOTTY_NET_MAX_PATH_LENGTH);
        if (length == 0 || length == KIOTTY_NET_MAX_PATH_LENGTH)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        wchar_t wide[KIOTTY_NET_MAX_PATH_LENGTH];
        if (!widen(path, wide))
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        HANDLE file = ::CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        LARGE_INTEGER size;
        if (::GetFileSizeEx(file, &size) == FALSE)
        {
            ::CloseHandle(file);
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        _handle = reinterpret_cast<intptr_t>(file);
        _size = static_cast<uint64_t>(size.QuadPart);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    FileReadResult InputFile::read(uint64_t offset, char* buffer, size_t length)
    {
        if (!valid())
        {
            return FileReadResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        size_t done = 0;
        while (done < length)
        {
            uint64_t position = offset + done;
            OVERLAPPED at{};
            at.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
            at.OffsetHigh = static_cast<DWORD>(position >> 32);
            size_t left = length - done;
            DWORD request = left < READ_CHUNK_BYTES ? static_cast<DWORD>(left) : READ_CHUNK_BYTES;
            DWORD got = 0;
            BOOL completed = ::ReadFile(fileOf(_handle), buffer + done, request, &got, &at);
            if (completed == FALSE && ::GetLastError() != ERROR_HANDLE_EOF)
            {
                return FileReadResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
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

    SocketCode sendFile(const ActiveSocket& socket, const InputFile& file, FileRange range, const Deadline& deadline)
    {
        if (!socket.valid() || !file.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const SOCKET out = static_cast<SOCKET>(socket.handle());
        const LPFN_TRANSMITFILE transmit = transmitFileOf(out);
        const ManualEvent done;
        if (transmit == nullptr || done.handle() == nullptr) { return SocketCode::OUT_OF_RESOURCE; }
        const Transmission transmission{ transmit, done, fileOf(file.handle()), out };
        uint64_t sent = 0;
        while (sent < range.length)
        {
            const uint64_t left = range.length - sent;
            const DWORD piece = left < TRANSMIT_CHUNK_BYTES ? static_cast<DWORD>(left) : TRANSMIT_CHUNK_BYTES;
            const Transmitted moved = transmitPiece(transmission, range.offset + sent, piece, deadline);
            if (!moved) { return moved.code; }
            sent += moved.value;
        }
        return SocketCode::SUCCESS;
    }
}

#endif
