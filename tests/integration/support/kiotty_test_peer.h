#if !defined(KIOTTY_TEST_PEER_H)
#define KIOTTY_TEST_PEER_H

#include "support/kiotty_test_os.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace kiotty_test
{
    class LoopbackPeer
    {
    public:
        LoopbackPeer();
        ~LoopbackPeer();

        LoopbackPeer(const LoopbackPeer&) = delete;
        LoopbackPeer& operator=(const LoopbackPeer&) = delete;

        bool     listenTcp(uint16_t port);
        bool     listenUnix(const std::string& path);
        uint16_t port() const;
        bool     accept(uint32_t timeout_ms);

        bool connectTcp(uint16_t port);
        bool connectUnix(const std::string& path);

        bool sendAll(const void* data, size_t length);
        bool recvExactly(void* data, size_t length, uint32_t timeout_ms);
        bool shutdownWrite();
        bool reset();
        void close();

    private:
        RawSocket   _listener;
        RawSocket   _connection;
        uint16_t    _port;
        std::string _path;
    };

    uint16_t    unusedPort();
    std::string uniqueSocketPath();
    void        copyHost(char* target, size_t capacity, const std::string& host);
}

#endif
