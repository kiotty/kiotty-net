#include "support/kiotty_test_port_lease.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace kiotty_test
{
    namespace
    {
        const char* const LOCK_FILE_NAME = "kiotty_test_ports.lock";

        std::string lockFilePath()
        {
#if defined(_WIN32)
            char directory[MAX_PATH + 1] = {};
            DWORD length = ::GetTempPathA(MAX_PATH, directory);
            if (length == 0 || length > MAX_PATH)
            {
                return std::string(LOCK_FILE_NAME);
            }
            return std::string(directory, length) + LOCK_FILE_NAME;
#else
            return std::string("/tmp/") + LOCK_FILE_NAME;
#endif
        }

        uint32_t parseCounter(const char* text, size_t length)
        {
            if (length == 0)
            {
                return 0;
            }
            return static_cast<uint32_t>(std::strtoul(text, nullptr, 10));
        }
    }

#if defined(_WIN32)
    PortLease::PortLease()
        : _file(::CreateFileA(lockFilePath().c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr))
    {
        if (_file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        OVERLAPPED whole = {};
        BOOL ok = ::LockFileEx(_file, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &whole);
        if (!ok)
        {
            ::CloseHandle(_file);
            _file = INVALID_HANDLE_VALUE;
        }
    }

    PortLease::~PortLease()
    {
        if (_file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        OVERLAPPED whole = {};
        ::UnlockFileEx(_file, 0, MAXDWORD, MAXDWORD, &whole);
        ::CloseHandle(_file);
    }

    bool PortLease::locked() const
    {
        return _file != INVALID_HANDLE_VALUE;
    }

    uint32_t PortLease::take()
    {
        char  text[16] = {};
        DWORD count = 0;
        ::SetFilePointer(_file, 0, nullptr, FILE_BEGIN);
        ::ReadFile(_file, text, sizeof(text) - 1, &count, nullptr);
        uint32_t counter = parseCounter(text, count);

        int length = std::snprintf(text, sizeof(text), "%u", counter + 1);
        DWORD written = 0;
        ::SetFilePointer(_file, 0, nullptr, FILE_BEGIN);
        ::SetEndOfFile(_file);
        ::WriteFile(_file, text, static_cast<DWORD>(length), &written, nullptr);
        return counter;
    }
#else
    PortLease::PortLease()
        : _file(::open(lockFilePath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666))
    {
        if (_file < 0)
        {
            return;
        }
        if (::flock(_file, LOCK_EX) != 0)
        {
            ::close(_file);
            _file = -1;
        }
    }

    PortLease::~PortLease()
    {
        if (_file < 0)
        {
            return;
        }
        ::flock(_file, LOCK_UN);
        ::close(_file);
    }

    bool PortLease::locked() const
    {
        return _file >= 0;
    }

    uint32_t PortLease::take()
    {
        char text[16] = {};
        ::lseek(_file, 0, SEEK_SET);
        ssize_t count = ::read(_file, text, sizeof(text) - 1);
        uint32_t counter = parseCounter(text, count > 0 ? static_cast<size_t>(count) : 0);

        int length = std::snprintf(text, sizeof(text), "%u", counter + 1);
        ::lseek(_file, 0, SEEK_SET);
        if (::ftruncate(_file, 0) != 0)
        {
            return counter;
        }
        ssize_t written = ::write(_file, text, static_cast<size_t>(length));
        (void)written;
        return counter;
    }
#endif
}
