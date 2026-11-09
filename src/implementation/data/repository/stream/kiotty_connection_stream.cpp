#include "data/repository/stream/kiotty_connection_stream.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t RESULT_MAXIMUM_BYTES = 0x7FFFFFFF;
        constexpr size_t TLS_WRITE_PIECE_BYTES = TLS_RECORD_PLAIN_BYTES;

        KiottyNetResult resultOf(size_t bytes, KiottyNetCode code);
        size_t          smallerOf(size_t left, size_t right);
        bool            isHardFailure(SocketCode code);
    }

    ConnectionStream::ConnectionStream(ActiveSocket& socket,
                                       TlsEngine& tls,
                                       StageBase& receive,
                                       StageBase& send)
        : _socket(socket)
        , _tls(tls)
        , _pipe(tls)
        , _receive(receive)
        , _send(send)
    {
    }

    KiottyNetResult ConnectionStream::read(Span<char> buffer, uint32_t timeout_milliseconds)
    {
        const bool is_empty = buffer.data == nullptr || buffer.length == 0;
        if (is_empty) { return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, flushed); }
        const size_t capped = smallerOf(buffer.length, RESULT_MAXIMUM_BYTES);
        const Span<char> target{ buffer.data, capped };
        return readInto(target, deadline);
    }

    KiottyNetResult ConnectionStream::write(KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        const bool missing = data.data == nullptr && data.length > 0;
        const bool too_large = data.length > RESULT_MAXIMUM_BYTES;
        if (too_large || missing) { return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, flushed); }
        return send(data, deadline);
    }

    KiottyNetCode ConnectionStream::writeFile(Span<const char> path,
                                              uint64_t offset,
                                              uint64_t length,
                                              uint32_t timeout_milliseconds)
    {
        InputFile file;
        const KiottyNetCode opened = file.open(path);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        const uint64_t size = file.size();
        if (offset > size || length > size - offset) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS) { return flushed; }
        const FileRange range{ offset, length };
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            return sendFileOverTls(file, range, deadline);
        }
        const SocketCode code = sendFile(_socket, file, range, deadline);
        if (code == SocketCode::INVALID_ARGUMENT) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return toNetCode(code);
    }

    bool ConnectionStream::connected() const
    {
        const bool valid = _socket.valid();
        if (!valid) { return false; }
        const size_t staged = _receive.consumer().readable();
        if (staged > 0) { return true; }
        char byte = 0;
        const Span<char> probe{ &byte, sizeof(byte) };
        const TransferResult peeked = implementation::peek(_socket, probe);
        if (peeked) { return true; }
        return peeked.code == SocketCode::WOULD_BLOCK;
    }

    KiottyNetCode ConnectionStream::handshake(uint32_t timeout_milliseconds)
    {
        const bool secure = _tls.status().is_active;
        if (!secure) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Deadline deadline(timeout_milliseconds);
        for (;;)
        {
            const TlsStep step = _tls.handshake();
            if (step == TlsStep::FAILED) { return _tls.status().failure.code; }
            if (step == TlsStep::DONE)
            {
                return KIOTTY_NET_CODE_SUCCESS;
            }
            const KiottyNetCode moved = exchangeHandshake(step, deadline);
            if (moved != KIOTTY_NET_CODE_SUCCESS) { return moved; }
        }
    }

    KiottyNetResult ConnectionStream::readInto(Span<char> target, const Deadline& deadline)
    {
        for (;;)
        {
            const size_t taken = takeReceived(target);
            if (taken > 0)
            {
                return resultOf(taken, KIOTTY_NET_CODE_SUCCESS);
            }
            const KiottyNetResult attempt = readOnce(target, deadline);
            if (attempt.code != KIOTTY_NET_CODE_SUCCESS || attempt.bytes > 0)
            {
                return attempt;
            }
        }
    }

    KiottyNetResult ConnectionStream::readOnce(Span<char> target, const Deadline& deadline)
    {
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            return readCipher(target, deadline);
        }
        return readPlain(target, deadline);
    }

    KiottyNetResult ConnectionStream::readCipher(Span<char> target, const Deadline& deadline)
    {
        const DecodeResult decoded = _tls.decode(target);
        const KiottyNetCode replied = flush(deadline);
        if (!decoded) { return resultOf(0, decoded.code); }
        if (replied != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, replied); }
        if (decoded.value > 0)
        {
            return resultOf(decoded.value, KIOTTY_NET_CODE_SUCCESS);
        }
        const SocketCode waited = waitReadable(deadline);
        if (waited != SocketCode::SUCCESS) { return resultOf(0, toNetCode(waited)); }
        const SocketCode filled = receiveCipher();
        const bool failed = isHardFailure(filled);
        if (failed) { return resultOf(0, toNetCode(filled)); }
        return resultOf(0, KIOTTY_NET_CODE_SUCCESS);
    }

    KiottyNetResult ConnectionStream::readPlain(Span<char> target, const Deadline& deadline)
    {
        const SocketCode waited = waitReadable(deadline);
        if (waited != SocketCode::SUCCESS) { return resultOf(0, toNetCode(waited)); }
        const TransferResult received = implementation::receive(_socket, target);
        if (received)
        {
            return resultOf(received.value, KIOTTY_NET_CODE_SUCCESS);
        }
        const bool failed = received.code != SocketCode::WOULD_BLOCK;
        if (failed) { return resultOf(0, toNetCode(received.code)); }
        return resultOf(0, KIOTTY_NET_CODE_SUCCESS);
    }

    KiottyNetCode ConnectionStream::sendFileOverTls(InputFile& file,
                                                    FileRange range,
                                                    const Deadline& deadline)
    {
        const size_t chunk = _send.capacity();
        const size_t staged = _send.consumer().readable();
        if (chunk == 0 || staged > 0) { return KIOTTY_NET_CODE_BUSY; }
        uint64_t done = 0;
        while (done < range.length)
        {
            const uint64_t left = range.length - done;
            const size_t piece = left < chunk ? static_cast<size_t>(left) : chunk;
            const FileRange part{ range.offset + done, piece };
            const KiottyNetCode sent = sendFilePiece(file, part, deadline);
            if (sent != KIOTTY_NET_CODE_SUCCESS) { return sent; }
            done += piece;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode ConnectionStream::sendFilePiece(InputFile& file,
                                                  FileRange piece,
                                                  const Deadline& deadline)
    {
        const size_t length = static_cast<size_t>(piece.length);
        _send.clear();
        StageBase::Provider provider = _send.provider();
        char* target = provider.prepare(length);
        const FileReadResult loaded = file.read(piece.offset, Span<char>{ target, length });
        if (!loaded || loaded.value != length)
        {
            _send.clear();
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        provider.commit(length);
        const KiottyNetResult sent = send(KiottyNetSpan{ target, length }, deadline);
        _send.clear();
        return sent.code;
    }

    KiottyNetCode ConnectionStream::exchangeHandshake(TlsStep step, const Deadline& deadline)
    {
        if (step == TlsStep::RECEIVE)
        {
            return receiveHandshake(deadline);
        }
        const KiottyNetCode flushed = flush(deadline);
        if (flushed == KIOTTY_NET_CODE_TIMEOUT) { return KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT; }
        return flushed;
    }

    KiottyNetCode ConnectionStream::receiveHandshake(const Deadline& deadline)
    {
        const SocketCode waited = waitReadable(deadline);
        if (waited == SocketCode::TIMEOUT) { return KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT; }
        if (waited != SocketCode::SUCCESS) { return toNetCode(waited); }
        const SocketCode filled = receiveCipher();
        const bool failed = isHardFailure(filled);
        if (failed) { return toNetCode(filled); }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode ConnectionStream::flush(const Deadline& deadline)
    {
        const bool secure = _tls.status().is_active;
        if (!secure)
        {
            return KIOTTY_NET_CODE_SUCCESS;
        }
        const KiottyNetCode drained = flushCipherStage(deadline);
        if (drained != KIOTTY_NET_CODE_SUCCESS) { return drained; }
        return flushPipe(deadline);
    }

    KiottyNetCode ConnectionStream::flushCipherStage(const Deadline& deadline)
    {
        StageBase& cipher = _tls.stages().send;
        StageBase::Consumer waiting = cipher.consumer();
        const size_t readable = waiting.readable();
        if (readable == 0)
        {
            return KIOTTY_NET_CODE_SUCCESS;
        }
        const Sent sent = sendAll(Span<const char>{ waiting.peek(), readable }, deadline);
        waiting.commit(sent.length);
        cipher.compact();
        return toNetCode(sent.code);
    }

    KiottyNetCode ConnectionStream::flushPipe(const Deadline& deadline)
    {
        Span<const char> outgoing = _pipe.outgoing();
        while (outgoing.length > 0)
        {
            const Sent sent = sendAll(outgoing, deadline);
            _pipe.sent(sent.length);
            if (sent.code != SocketCode::SUCCESS) { return toNetCode(sent.code); }
            outgoing = _pipe.outgoing();
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult ConnectionStream::send(KiottyNetSpan data, const Deadline& deadline)
    {
        const bool secure = _tls.status().is_active;
        if (!secure)
        {
            const Sent sent = sendAll(Span<const char>{ data.data, data.length }, deadline);
            return resultOf(sent.length, toNetCode(sent.code));
        }
        size_t done = 0;
        while (done < data.length)
        {
            const size_t piece = smallerOf(TLS_WRITE_PIECE_BYTES, data.length - done);
            const KiottyNetSpan part{ data.data + done, piece };
            const KiottyNetCode written = sendRecord(part, deadline);
            if (written != KIOTTY_NET_CODE_SUCCESS) { return resultOf(done, written); }
            done += piece;
        }
        return resultOf(done, KIOTTY_NET_CODE_SUCCESS);
    }

    KiottyNetCode ConnectionStream::sendRecord(KiottyNetSpan part, const Deadline& deadline)
    {
        TlsStep step = TlsStep::SEND;
        while (step == TlsStep::SEND)
        {
            step = _pipe.write(part);
            const KiottyNetCode flushed = flush(deadline);
            if (step == TlsStep::FAILED) { return _tls.status().failure.code; }
            if (flushed != KIOTTY_NET_CODE_SUCCESS) { return flushed; }
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    ConnectionStream::Sent ConnectionStream::sendAll(Span<const char> data,
                                                     const Deadline& deadline)
    {
        size_t sent = 0;
        while (sent < data.length)
        {
            const TransferResult moved = implementation::send(_socket, data.skip(sent));
            if (moved)
            {
                sent += moved.value;
                continue;
            }
            const SocketCode waited = awaitWritable(moved.code, deadline);
            if (waited != SocketCode::SUCCESS) { return Sent{ sent, waited }; }
        }
        return Sent{ sent, SocketCode::SUCCESS };
    }

    SocketCode ConnectionStream::awaitWritable(SocketCode code, const Deadline& deadline)
    {
        if (code != SocketCode::WOULD_BLOCK) { return code; }
        const bool expired = deadline.expired();
        if (expired) { return SocketCode::TIMEOUT; }
        return implementation::waitWritable(_socket, deadline.remaining());
    }

    SocketCode ConnectionStream::waitReadable(const Deadline& deadline)
    {
        const bool expired = deadline.expired();
        if (expired) { return SocketCode::TIMEOUT; }
        return implementation::waitReadable(_socket, deadline.remaining());
    }

    SocketCode ConnectionStream::receiveCipher()
    {
        const Span<char> room = _pipe.receiveRoom();
        if (room.length == 0)
        {
            return receiveIntoStage();
        }
        const TransferResult received = implementation::receive(_socket, room);
        if (!received) { return received.code; }
        _pipe.received(received.value);
        return SocketCode::SUCCESS;
    }

    SocketCode ConnectionStream::receiveIntoStage()
    {
        StageBase& cipher = _tls.stages().receive;
        cipher.compact();
        StageBase::Provider provider = cipher.provider();
        const size_t room = provider.writable();
        if (room == 0) { return SocketCode::OUT_OF_RESOURCE; }
        char* target = provider.prepare(room);
        const Span<char> space{ target, room };
        const TransferResult received = implementation::receive(_socket, space);
        if (!received) { return received.code; }
        provider.commit(received.value);
        return SocketCode::SUCCESS;
    }

    size_t ConnectionStream::takeReceived(Span<char> target)
    {
        StageBase::Consumer consumer = _receive.consumer();
        const size_t taken = smallerOf(consumer.readable(), target.length);
        if (taken == 0) { return 0; }
        const bool copied = target.copyFrom(Span<const char>{ consumer.peek(), taken });
        if (!copied) { return 0; }
        consumer.commit(taken);
        _receive.compact();
        return taken;
    }

    namespace
    {
        KiottyNetResult resultOf(size_t bytes, KiottyNetCode code)
        {
            KiottyNetResult result{};
            result.bytes = static_cast<int32_t>(bytes);
            result.code = code;
            return result;
        }

        size_t smallerOf(size_t left, size_t right)
        {
            return left < right ? left : right;
        }

        bool isHardFailure(SocketCode code)
        {
            return code != SocketCode::SUCCESS && code != SocketCode::WOULD_BLOCK;
        }
    }
}
