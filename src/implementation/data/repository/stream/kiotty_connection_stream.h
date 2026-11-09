#pragma once

#include "core/kiotty_deadline.h"
#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "data/infra/file/kiotty_input_file.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_pipe.h"
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

        KiottyNetResult read(Span<char> buffer, uint32_t timeout_milliseconds);
        KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
        KiottyNetCode   writeFile(Span<const char> path,
                                  uint64_t offset,
                                  uint64_t length,
                                  uint32_t timeout_milliseconds);
        bool            connected() const;
        KiottyNetCode   handshake(uint32_t timeout_milliseconds);

    private:
        struct Sent
        {
            size_t     length;
            SocketCode code;
        };

        static_assert(sizeof(Sent) == 2 * sizeof(size_t),
                      "ConnectionStream::Sent: unexpected padding");

        KiottyNetResult readInto(Span<char> target, const Deadline& deadline);
        KiottyNetResult readOnce(Span<char> target, const Deadline& deadline);
        KiottyNetResult readCipher(Span<char> target, const Deadline& deadline);
        KiottyNetResult readPlain(Span<char> target, const Deadline& deadline);
        KiottyNetCode   sendFileOverTls(InputFile& file, FileRange range, const Deadline& deadline);
        KiottyNetCode   sendFilePiece(InputFile& file, FileRange piece, const Deadline& deadline);
        KiottyNetCode   exchangeHandshake(TlsStep step, const Deadline& deadline);
        KiottyNetCode   receiveHandshake(const Deadline& deadline);
        KiottyNetCode   flush(const Deadline& deadline);
        KiottyNetCode   flushCipherStage(const Deadline& deadline);
        KiottyNetCode   flushPipe(const Deadline& deadline);
        KiottyNetResult send(KiottyNetSpan data, const Deadline& deadline);
        KiottyNetCode   sendRecord(KiottyNetSpan part, const Deadline& deadline);
        Sent            sendAll(Span<const char> data, const Deadline& deadline);
        SocketCode      awaitWritable(SocketCode code, const Deadline& deadline);
        SocketCode      waitReadable(const Deadline& deadline);
        SocketCode      receiveCipher();
        SocketCode      receiveIntoStage();
        size_t          takeReceived(Span<char> target);

        ActiveSocket& _socket;
        TlsEngine&    _tls;
        TlsPipe       _pipe;
        StageBase&    _receive;
        StageBase&    _send;
    };
}
