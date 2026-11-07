#include "base/stream/kiotty_connection_stream.h"

#include "data/infra/file/kiotty_input_file.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        const size_t RESULT_MAXIMUM_BYTES = 0x7FFFFFFF;
        const size_t TLS_WRITE_PIECE_BYTES = TLS_RECORD_PLAIN_BYTES;

        KiottyNetResult resultOf(size_t bytes, KiottyNetCode code)
        {
            KiottyNetResult result;
            result.bytes = static_cast<int32_t>(bytes);
            result.code = code;
            return result;
        }

        size_t smallerOf(size_t left, size_t right)
        {
            return left < right ? left : right;
        }
    }

    ConnectionStream::ConnectionStream(ActiveSocket& socket, TlsEngine& tls, StageBase& receive, StageBase& send)
        : _socket(socket)
        , _tls(tls)
        , _pipe(tls)
        , _receive(receive)
        , _send(send)
    {
    }

    KiottyNetResult ConnectionStream::read(void* buffer, size_t length, uint32_t timeout_milliseconds)
    {
        if (buffer == nullptr || length == 0)
        {
            return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        Deadline deadline(timeout_milliseconds);
        KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS)
        {
            return resultOf(0, flushed);
        }
        size_t limit = smallerOf(length, RESULT_MAXIMUM_BYTES);
        for (;;)
        {
            size_t taken = takeReceived(buffer, limit);
            if (taken > 0)
            {
                return resultOf(taken, KIOTTY_NET_CODE_SUCCESS);
            }
            if (_tls.active())
            {
                char* target = static_cast<char*>(buffer);
                DecodeResult decoded = _tls.decode(target, limit);
                KiottyNetCode replied = flush(deadline);
                if (!decoded)
                {
                    return resultOf(0, decoded.code);
                }
                if (replied != KIOTTY_NET_CODE_SUCCESS)
                {
                    return resultOf(0, replied);
                }
                if (decoded.value > 0)
                {
                    return resultOf(decoded.value, KIOTTY_NET_CODE_SUCCESS);
                }
            }
            SocketCode waited = waitReadable(deadline);
            if (waited != SocketCode::SUCCESS)
            {
                return resultOf(0, toNetCode(waited));
            }
            if (_tls.active())
            {
                SocketCode filled = receiveCipher();
                if (filled != SocketCode::SUCCESS && filled != SocketCode::WOULD_BLOCK)
                {
                    return resultOf(0, toNetCode(filled));
                }
                continue;
            }
            TransferResult received = implementation::receive(_socket, Span<char>{ static_cast<char*>(buffer), limit });
            if (received && received.value > 0)
            {
                return resultOf(received.value, KIOTTY_NET_CODE_SUCCESS);
            }
            if (!received && received.code != SocketCode::WOULD_BLOCK)
            {
                return resultOf(0, toNetCode(received.code));
            }
        }
    }

    KiottyNetResult ConnectionStream::write(KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        if (data.length > RESULT_MAXIMUM_BYTES || (data.data == nullptr && data.length > 0))
        {
            return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        Deadline deadline(timeout_milliseconds);
        KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS)
        {
            return resultOf(0, flushed);
        }
        return send(data, deadline);
    }

    KiottyNetCode ConnectionStream::writeFile(const char* path, uint64_t offset, uint64_t length, uint32_t timeout_milliseconds)
    {
        InputFile file;
        KiottyNetCode opened = file.open(path);
        if (opened != KIOTTY_NET_CODE_SUCCESS)
        {
            return opened;
        }
        uint64_t size = file.size();
        if (offset > size || length > size - offset)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        Deadline deadline(timeout_milliseconds);
        KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS)
        {
            return flushed;
        }
        if (!_tls.active())
        {
            const SocketCode code = sendFile(_socket, file, FileRange{ offset, length }, deadline);
            if (code == SocketCode::INVALID_ARGUMENT)
            {
                return KIOTTY_NET_CODE_INVALID_ARGUMENT;
            }
            return toNetCode(code);
        }
        size_t chunk = _send.capacity();
        if (chunk == 0 || _send.consumer().readable() > 0)
        {
            return KIOTTY_NET_CODE_BUSY;
        }
        uint64_t done = 0;
        while (done < length)
        {
            uint64_t left = length - done;
            size_t piece = left < chunk ? static_cast<size_t>(left) : chunk;
            _send.clear();
            StageBase::Provider provider = _send.provider();
            char* target = provider.prepare(piece);
            FileReadResult loaded = file.read(offset + done, target, piece);
            if (!loaded || loaded.value != piece)
            {
                _send.clear();
                return KIOTTY_NET_CODE_INVALID_ARGUMENT;
            }
            provider.commit(piece);
            KiottyNetSpan part = { target, piece };
            KiottyNetResult sent = send(part, deadline);
            _send.clear();
            if (sent.code != KIOTTY_NET_CODE_SUCCESS)
            {
                return sent.code;
            }
            done += piece;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool ConnectionStream::connected() const
    {
        if (!_socket.valid())
        {
            return false;
        }
        if (_receive.consumer().readable() > 0)
        {
            return true;
        }
        char byte = 0;
        TransferResult peeked = implementation::peek(_socket, Span<char>{ &byte, sizeof(byte) });
        if (peeked)
        {
            return true;
        }
        return peeked.code == SocketCode::WOULD_BLOCK;
    }

    KiottyNetCode ConnectionStream::handshake(uint32_t timeout_milliseconds)
    {
        if (!_tls.active())
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        Deadline deadline(timeout_milliseconds);
        for (;;)
        {
            TlsStep step = _tls.handshake();
            if (step == TlsStep::FAILED)
            {
                return _tls.failure().code;
            }
            if (step == TlsStep::DONE)
            {
                return KIOTTY_NET_CODE_SUCCESS;
            }
            if (step == TlsStep::SEND)
            {
                KiottyNetCode flushed = flush(deadline);
                if (flushed == KIOTTY_NET_CODE_TIMEOUT)
                {
                    return KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT;
                }
                if (flushed != KIOTTY_NET_CODE_SUCCESS)
                {
                    return flushed;
                }
                continue;
            }
            SocketCode waited = waitReadable(deadline);
            if (waited == SocketCode::TIMEOUT)
            {
                return KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT;
            }
            if (waited != SocketCode::SUCCESS)
            {
                return toNetCode(waited);
            }
            SocketCode filled = receiveCipher();
            if (filled != SocketCode::SUCCESS && filled != SocketCode::WOULD_BLOCK)
            {
                return toNetCode(filled);
            }
        }
    }

    KiottyNetCode ConnectionStream::flush(const Deadline& deadline)
    {
        if (!_tls.active())
        {
            return KIOTTY_NET_CODE_SUCCESS;
        }
        StageBase& cipher = _tls.cipherSend();
        StageBase::Consumer waiting = cipher.consumer();
        size_t readable = waiting.readable();
        if (readable > 0)
        {
            size_t sent = 0;
            SocketCode code = sendAll(waiting.peek(), readable, deadline, sent);
            waiting.commit(sent);
            cipher.compact();
            if (code != SocketCode::SUCCESS)
            {
                return toNetCode(code);
            }
        }
        Span<const char> outgoing = _pipe.outgoing();
        while (!outgoing.empty())
        {
            size_t sent = 0;
            SocketCode code = sendAll(outgoing.data, outgoing.length, deadline, sent);
            _pipe.sent(sent);
            if (code != SocketCode::SUCCESS)
            {
                return toNetCode(code);
            }
            outgoing = _pipe.outgoing();
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult ConnectionStream::send(KiottyNetSpan data, const Deadline& deadline)
    {
        if (!_tls.active())
        {
            size_t sent = 0;
            SocketCode code = sendAll(data.data, data.length, deadline, sent);
            return resultOf(sent, toNetCode(code));
        }
        size_t done = 0;
        while (done < data.length)
        {
            size_t piece = smallerOf(TLS_WRITE_PIECE_BYTES, data.length - done);
            KiottyNetSpan part = { data.data + done, piece };
            TlsStep step = TlsStep::SEND;
            while (step == TlsStep::SEND)
            {
                step = _pipe.write(part);
                KiottyNetCode flushed = flush(deadline);
                if (step == TlsStep::FAILED)
                {
                    return resultOf(done, _tls.failure().code);
                }
                if (flushed != KIOTTY_NET_CODE_SUCCESS)
                {
                    return resultOf(done, flushed);
                }
            }
            done += piece;
        }
        return resultOf(done, KIOTTY_NET_CODE_SUCCESS);
    }

    SocketCode ConnectionStream::sendAll(const char* data, size_t length, const Deadline& deadline, size_t& sent)
    {
        sent = 0;
        while (sent < length)
        {
            TransferResult moved = implementation::send(_socket, Span<const char>{ data + sent, length - sent });
            if (moved)
            {
                sent += moved.value;
                continue;
            }
            if (moved.code != SocketCode::WOULD_BLOCK)
            {
                return moved.code;
            }
            if (deadline.expired())
            {
                return SocketCode::TIMEOUT;
            }
            SocketCode waited = implementation::waitWritable(_socket, deadline.remaining());
            if (waited != SocketCode::SUCCESS)
            {
                return waited;
            }
        }
        return SocketCode::SUCCESS;
    }

    SocketCode ConnectionStream::waitReadable(const Deadline& deadline)
    {
        if (deadline.expired())
        {
            return SocketCode::TIMEOUT;
        }
        return implementation::waitReadable(_socket, deadline.remaining());
    }

    SocketCode ConnectionStream::receiveCipher()
    {
        Span<char> room = _pipe.receiveRoom();
        if (room.empty())
        {
            return receiveIntoStage();
        }
        TransferResult received = implementation::receive(_socket, Span<char>{ room.data, room.length });
        if (!received)
        {
            return received.code;
        }
        _pipe.received(received.value);
        return SocketCode::SUCCESS;
    }

    SocketCode ConnectionStream::receiveIntoStage()
    {
        StageBase& cipher = _tls.cipherReceive();
        cipher.compact();
        StageBase::Provider provider = cipher.provider();
        size_t room = provider.writable();
        if (room == 0)
        {
            return SocketCode::OUT_OF_RESOURCE;
        }
        char* target = provider.prepare(room);
        TransferResult received = implementation::receive(_socket, Span<char>{ target, room });
        if (!received)
        {
            return received.code;
        }
        provider.commit(received.value);
        return SocketCode::SUCCESS;
    }

    size_t ConnectionStream::takeReceived(void* buffer, size_t length)
    {
        StageBase::Consumer consumer = _receive.consumer();
        size_t taken = smallerOf(consumer.readable(), length);
        if (taken == 0)
        {
            return 0;
        }
        std::memcpy(buffer, consumer.peek(), taken);
        consumer.commit(taken);
        _receive.compact();
        return taken;
    }
}
