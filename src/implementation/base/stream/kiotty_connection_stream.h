#if !defined(KIOTTY_CONNECTION_STREAM_H)
#define KIOTTY_CONNECTION_STREAM_H

#include "base/socket/kiotty_socket.h"
#include "base/tls/kiotty_tls_engine.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        class ConnectionStream
        {
        public:
            ConnectionStream(ActiveSocket& socket, TlsEngine& tls, StageBase& receive, StageBase& send);
            ConnectionStream(const ConnectionStream& other) = default;
            ConnectionStream& operator=(const ConnectionStream&) = delete;

            KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);
            KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);
            KiottyNetCode   writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_ms);
            bool            connected() const;
            KiottyNetCode   handshake(uint32_t timeout_ms);

        private:
            KiottyNetCode   flush(const Deadline& deadline);
            KiottyNetResult send(KiottyNetSpan data, const Deadline& deadline);
            SocketCode      sendAll(const char* data, size_t length, const Deadline& deadline, size_t& sent);
            SocketCode      waitReadable(const Deadline& deadline);
            SocketCode      receiveCipher();
            size_t          takeReceived(void* buffer, size_t length);

            ActiveSocket& _socket;
            TlsEngine&    _tls;
            StageBase&    _receive;
            StageBase&    _send;
        };
    }
}

#endif
