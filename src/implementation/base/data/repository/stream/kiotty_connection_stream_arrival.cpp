#include "base/data/repository/stream/kiotty_connection_stream.h"

#include "core/kiotty_codes.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    KiottyNetResult ConnectionStream::sendWatched(KiottyNetSpan data, const Deadline& deadline)
    {
        const KiottyNetCode arrived = takeArrival();
        if (arrived != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, arrived); }
        return send(data, deadline);
    }

    KiottyNetResult ConnectionStream::sendPlain(KiottyNetSpan data, const Deadline& deadline)
    {
        size_t done = 0;
        for (;;)
        {
            const Span<const char> rest{ data.data + done, data.length - done };
            const Sent sent = sendAll(rest, deadline);
            done += sent.length;
            if (sent.code != SocketCode::READABLE) { return resultOf(done, toNetCode(sent.code)); }
            const KiottyNetCode arrived = takeArrival();
            if (arrived != KIOTTY_NET_CODE_SUCCESS) { return resultOf(done, arrived); }
        }
    }

    SocketCode ConnectionStream::awaitSendable(SocketCode code, const Deadline& deadline)
    {
        if (code != SocketCode::WOULD_BLOCK) { return code; }
        const bool expired = deadline.expired();
        if (expired) { return SocketCode::TIMEOUT; }
        const bool is_watching = _watch == ArrivalWatch::ON && hasArrivalRoom();
        if (is_watching) { return waitWritableOrReadable(_socket, deadline.bound(0)); }
        return implementation::waitWritable(_socket, deadline.bound(0));
    }

    bool ConnectionStream::hasArrivalRoom()
    {
        _receive.compact();
        return _receive.provider().writable() > 0;
    }

    KiottyNetCode ConnectionStream::takeArrival()
    {
        const KiottyNetCode looked = absorb();
        if (looked != KIOTTY_NET_CODE_BUFFER_FULL) { return looked; }
        _watch = ArrivalWatch::OFF;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode ConnectionStream::absorb()
    {
        if (!hasArrivalRoom()) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const bool secure = _tls.status().is_active;
        return secure ? absorbCipher() : absorbPlain();
    }

    KiottyNetCode ConnectionStream::absorbPlain()
    {
        StageBase::Provider provider = _receive.provider();
        const size_t room = std::min(provider.writable(), RESULT_MAXIMUM_BYTES);
        const Span<char> space{ provider.prepare(room), room };
        const TransferResult received = implementation::receive(_socket, space);
        if (received)
        {
            provider.commit(received.value);
            return KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING;
        }
        if (received.code == SocketCode::WOULD_BLOCK) { return KIOTTY_NET_CODE_SUCCESS; }
        return toNetCode(received.code);
    }

    KiottyNetCode ConnectionStream::absorbCipher()
    {
        const KiottyNetCode waiting = decodeArrival();
        if (waiting != KIOTTY_NET_CODE_SUCCESS) { return waiting; }
        const SocketCode filled = receiveCipher();
        if (filled == SocketCode::WOULD_BLOCK) { return KIOTTY_NET_CODE_SUCCESS; }
        if (filled == SocketCode::OUT_OF_RESOURCE) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        if (filled != SocketCode::SUCCESS) { return toNetCode(filled); }
        return decodeArrival();
    }

    KiottyNetCode ConnectionStream::decodeArrival()
    {
        const DecodeResult decoded = _tls.decode(_receive);
        if (!decoded) { return decoded.code; }
        const bool is_plain = decoded.value > 0;
        return is_plain ? KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING : KIOTTY_NET_CODE_SUCCESS;
    }
}
