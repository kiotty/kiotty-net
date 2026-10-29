#if !defined(KIOTTY_SOCKET_H)
#define KIOTTY_SOCKET_H

#include "base/error/kiotty_socket_error.h"
#include "core/kiotty_result.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        typedef intptr_t SocketHandle;

        static const SocketHandle INVALID_SOCKET_HANDLE = -1;

        class ActiveSocket;

        typedef Result<SocketCode, ActiveSocket> ActiveSocketResult;
        typedef Result<SocketCode, size_t>       TransferResult;

        class ActiveSocket
        {
        public:
            ActiveSocket();
            ~ActiveSocket();

            ActiveSocket(ActiveSocket&& other) noexcept;
            ActiveSocket& operator=(ActiveSocket&& other) noexcept;

            ActiveSocket(const ActiveSocket&) = delete;
            ActiveSocket& operator=(const ActiveSocket&) = delete;

            static ActiveSocketResult open(KiottyNetTransport transport);

            SocketCode     connect(const KiottyNetEndpoint& endpoint, uint32_t timeout_ms);
            SocketCode     setNoDelay(bool on);
            TransferResult send(const void* data, size_t length);
            TransferResult receive(void* data, size_t length);
            SocketCode     shutdown();
            void           close();

            bool                    valid() const;
            SocketHandle            handle() const;
            KiottyNetConnectionInfo peer() const;

        private:
            friend class PassiveSocket;

            ActiveSocket(SocketHandle handle, KiottyNetTransport transport);

            SocketHandle            _handle;
            KiottyNetTransport      _transport;
            KiottyNetConnectionInfo _peer;
        };

        class PassiveSocket
        {
        public:
            PassiveSocket();
            ~PassiveSocket();

            PassiveSocket(const PassiveSocket&) = delete;
            PassiveSocket& operator=(const PassiveSocket&) = delete;

            SocketCode         open(const KiottyNetEndpoint& endpoint, int backlog);
            ActiveSocketResult accept();
            void               close();

            bool         valid() const;
            SocketHandle handle() const;
            uint16_t     port() const;

        private:
            SocketHandle       _handle;
            KiottyNetTransport _transport;
            uint16_t           _port;
        };

        Result<KiottyNetCode, ActiveSocket> connectWithRetry(const KiottyNetClientConfig& config, const std::atomic<bool>& stop);
    }
}

#endif
