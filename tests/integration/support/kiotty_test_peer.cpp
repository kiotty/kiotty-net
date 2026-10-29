#include "support/kiotty_test_peer.h"

#include <atomic>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <process.h>
#endif

namespace kiotty_test
{
    namespace
    {
        sockaddr_in loopbackAddress(uint16_t port)
        {
            sockaddr_in address;
            std::memset(&address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            return address;
        }

        sockaddr_un unixAddress(const std::string& path)
        {
            sockaddr_un address;
            std::memset(&address, 0, sizeof(address));
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size());
            return address;
        }

        int processId()
        {
#if defined(_WIN32)
            return _getpid();
#else
            return static_cast<int>(::getpid());
#endif
        }
    }

    LoopbackPeer::LoopbackPeer()
        : _listener(INVALID_RAW_SOCKET)
        , _connection(INVALID_RAW_SOCKET)
        , _port(0)
    {
    }

    LoopbackPeer::~LoopbackPeer()
    {
        close();
    }

    bool LoopbackPeer::listenTcp(uint16_t port)
    {
        _listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_listener == INVALID_RAW_SOCKET)
        {
            return false;
        }
        sockaddr_in address = loopbackAddress(port);
        if (::bind(_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            return false;
        }
        if (::listen(_listener, 16) != 0)
        {
            return false;
        }
        sockaddr_in bound;
        std::memset(&bound, 0, sizeof(bound));
        socklen_t length = sizeof(bound);
        ::getsockname(_listener, reinterpret_cast<sockaddr*>(&bound), &length);
        _port = ntohs(bound.sin_port);
        return true;
    }

    bool LoopbackPeer::listenUnix(const std::string& path)
    {
        removeFile(path.c_str());
        _listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (_listener == INVALID_RAW_SOCKET)
        {
            return false;
        }
        sockaddr_un address = unixAddress(path);
        if (::bind(_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            return false;
        }
        _path = path;
        return ::listen(_listener, 16) == 0;
    }

    uint16_t LoopbackPeer::port() const
    {
        return _port;
    }

    bool LoopbackPeer::accept(uint32_t timeout_ms)
    {
        if (!waitReadable(_listener, timeout_ms))
        {
            return false;
        }
        _connection = ::accept(_listener, nullptr, nullptr);
        return _connection != INVALID_RAW_SOCKET;
    }

    bool LoopbackPeer::connectTcp(uint16_t port)
    {
        _connection = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_connection == INVALID_RAW_SOCKET)
        {
            return false;
        }
        sockaddr_in address = loopbackAddress(port);
        return ::connect(_connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }

    bool LoopbackPeer::connectUnix(const std::string& path)
    {
        _connection = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (_connection == INVALID_RAW_SOCKET)
        {
            return false;
        }
        sockaddr_un address = unixAddress(path);
        return ::connect(_connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }

    bool LoopbackPeer::sendAll(const void* data, size_t length)
    {
        const char* bytes = static_cast<const char*>(data);
        size_t done = 0;
        while (done < length)
        {
            int sent = static_cast<int>(::send(_connection, bytes + done, static_cast<int>(length - done), 0));
            if (sent <= 0)
            {
                return false;
            }
            done += static_cast<size_t>(sent);
        }
        return true;
    }

    bool LoopbackPeer::recvExactly(void* data, size_t length, uint32_t timeout_ms)
    {
        char* bytes = static_cast<char*>(data);
        size_t done = 0;
        while (done < length)
        {
            if (!waitReadable(_connection, timeout_ms))
            {
                return false;
            }
            int received = static_cast<int>(::recv(_connection, bytes + done, static_cast<int>(length - done), 0));
            if (received <= 0)
            {
                return false;
            }
            done += static_cast<size_t>(received);
        }
        return true;
    }

    bool LoopbackPeer::shutdownWrite()
    {
#if defined(_WIN32)
        return ::shutdown(_connection, SD_SEND) == 0;
#else
        return ::shutdown(_connection, SHUT_WR) == 0;
#endif
    }

    void LoopbackPeer::close()
    {
        closeRaw(_connection);
        closeRaw(_listener);
        _connection = INVALID_RAW_SOCKET;
        _listener = INVALID_RAW_SOCKET;
        if (!_path.empty())
        {
            removeFile(_path.c_str());
            _path.clear();
        }
    }

    uint16_t unusedPort()
    {
        RawSocket probe = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address = loopbackAddress(0);
        ::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        sockaddr_in bound;
        std::memset(&bound, 0, sizeof(bound));
        socklen_t length = sizeof(bound);
        ::getsockname(probe, reinterpret_cast<sockaddr*>(&bound), &length);
        closeRaw(probe);
        return ntohs(bound.sin_port);
    }

    std::string uniqueSocketPath()
    {
        static std::atomic<int> counter(0);
        int number = counter.fetch_add(1);
#if defined(_WIN32)
        std::string path = "kt_";
#else
        std::string path = "/tmp/kt_";
#endif
        path += std::to_string(processId());
        path += "_";
        path += std::to_string(number);
        path += ".sock";
        removeFile(path.c_str());
        return path;
    }

    void copyHost(char* target, size_t capacity, const std::string& host)
    {
        std::memset(target, 0, capacity);
        size_t length = host.size();
        if (length > capacity - 1)
        {
            length = capacity - 1;
        }
        std::memcpy(target, host.c_str(), length);
    }
}
