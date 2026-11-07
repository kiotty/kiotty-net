#if !defined(KIOTTY_CONNECTION_STREAM_H)
#define KIOTTY_CONNECTION_STREAM_H

#include "base/socket/kiotty_socket.h"
#include "base/tls/kiotty_tls_engine.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class ConnectionStream
    {
    public:
        ConnectionStream(ActiveSocket& socket, TlsEngine& tls, StageBase& receive, StageBase& send);
        ConnectionStream(const ConnectionStream& other) = default;
        ConnectionStream& operator=(const ConnectionStream&) = delete;

        KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_milliseconds);
        KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
        KiottyNetCode   writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_milliseconds);
        bool            connected() const;
        KiottyNetCode   handshake(uint32_t timeout_milliseconds);

    private:
        KiottyNetCode   flush(const Deadline& deadline);
        KiottyNetResult send(KiottyNetSpan data, const Deadline& deadline);
        SocketCode      sendAll(const char* data, size_t length, const Deadline& deadline, size_t& sent);
        SocketCode      waitReadable(const Deadline& deadline);
        SocketCode      receiveCipher();
        SocketCode      receiveIntoStage();
        size_t          takeReceived(void* buffer, size_t length);

        ActiveSocket& _socket;
        TlsEngine&    _tls;
        StageBase&    _receive;
        StageBase&    _send;
    };
}

#endif
