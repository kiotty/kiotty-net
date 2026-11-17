#include "http/data/repository/server/kiotty_http_work.h"

#include "base/data/repository/endpoint/kiotty_endpoint.h"
#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_text.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "http/data/repository/response/kiotty_http_send_rules.h"
#include "http/data/repository/response/kiotty_http_response_text_builder.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   WHOLE = std::numeric_limits<size_t>::max();
        constexpr size_t   MAX_CHUNK_BYTES = 0x7FFFFFFF;
        constexpr uint32_t SHORTEST_WAIT_MILLISECONDS = 1;

        size_t                    openStream(HttpSession& session);
        KiottyNetCode             sendChunk(HttpSession& session,
                                            Span<const char> data,
                                            size_t kept,
                                            const Deadline& deadline);
        KiottyNetCode             sendPieces(HttpSession& session,
                                             Span<const char> data,
                                             size_t kept,
                                             const Deadline& deadline);
        KiottyNetCode             sendHeaderOnly(HttpSession& session,
                                                 size_t kept,
                                                 const Deadline& deadline);
        KiottyNetCode             sendSpan(const HttpSession& session,
                                           Span<const char> data,
                                           const Deadline& deadline);
        KiottyNetCode             failureOf(const HttpSession& session, KiottyNetCode code);
        KiottyNetCode             handOver(HttpSession& session,
                                           KiottyNetConnectionStream stream,
                                           const KiottyNetConnectionAction& then);
        KiottyNetConnectionAction finishingActionOf(HttpSession& session, SessionPhase phase);
        KiottyNetConnectionAction sealedPieceOf(HttpSession& session);
        KiottyNetConnectionAction firstPieceOf(HttpSession& session, size_t sent);
        KiottyNetConnectionAction nextExchangeOf(HttpSession& session);
        size_t                    headerBytesOf(const HttpSession& session);
    }

    HttpWork::HttpWork(HttpSessionHost& host)
        : _host(host)
    {
    }

    SessionResult HttpWork::fill(int64_t id)
    {
        const SessionResult found = _host.sessions.find(id);
        if (!found) { return found; }
        const SessionPhase phase = found.value().phase;
        const bool is_open = phase == SessionPhase::FILLING || phase == SessionPhase::WORKING;
        if (!is_open) { return SessionResult::failure(KIOTTY_NET_CODE_BUSY); }
        return found;
    }

    KiottyNetCode HttpWork::start(int64_t id)
    {
        const SessionResult found = _host.sessions.find(id);
        if (!found) { return found.code; }
        HttpSession& session = found.value();
        const bool is_filling = session.phase == SessionPhase::FILLING;
        const bool is_first = is_filling && !session.is_work_requested;
        if (!is_first) { return KIOTTY_NET_CODE_BUSY; }
        if (session.route.onWorkStarted == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        session.is_work_requested = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult HttpWork::write(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        const SessionResult found = working(id);
        if (!found) { return resultOf(0, found.code); }
        HttpSession& session = found.value();
        const Span<const char> piece{ data.data, data.length };
        const bool is_too_large = piece.length > MAX_CHUNK_BYTES;
        if (is_too_large) { return resultOf(0, KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const KiottyNetCode checked = session.response.frame().checkChunk(piece);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, checked); }
        if (session.stream_failure != KIOTTY_NET_CODE_SUCCESS)
        {
            return resultOf(0, session.stream_failure);
        }
        const Deadline deadline(timeout_milliseconds);
        const size_t kept = openStream(session);
        const bool has_body = session.response.frame().hasSendableBody();
        const KiottyNetCode sent = has_body ? sendChunk(session, piece, kept, deadline)
                                            : sendHeaderOnly(session, kept, deadline);
        session.stream_failure = failureOf(session, sent);
        const size_t bytes = sent == KIOTTY_NET_CODE_SUCCESS ? piece.length : 0;
        return resultOf(bytes, session.stream_failure);
    }

    KiottyNetCode HttpWork::finishWithResponse(int64_t id)
    {
        const SessionResult found = working(id);
        if (!found) { return found.code; }
        HttpSession& session = found.value();
        const SessionPhase phase = session.phase;
        const KiottyNetConnectionStream stream = session.stream;
        session.phase = SessionPhase::SENDING;
        const KiottyNetConnectionAction then = finishingActionOf(session, phase);
        return handOver(session, stream, then);
    }

    KiottyNetCode HttpWork::finishWithClose(int64_t id, KiottyNetCode reason)
    {
        const SessionResult found = working(id);
        if (!found) { return found.code; }
        HttpSession& session = found.value();
        const KiottyNetConnectionStream stream = session.stream;
        session.phase = SessionPhase::SENDING;
        return handOver(session, stream, thenClose(reason));
    }

    bool HttpWork::isConnected(int64_t id)
    {
        const SessionResult found = working(id);
        if (!found) { return false; }
        if (_host.is_closing.load()) { return false; }
        const KiottyNetConnectionStream stream = found.value().stream;
        return stream.owner->isStreamOpen(stream.id);
    }

    SessionResult HttpWork::working(int64_t id)
    {
        const SessionResult found = _host.sessions.find(id);
        if (!found) { return found; }
        const HttpSession& session = found.value();
        const SessionPhase phase = session.phase;
        const bool is_working = phase == SessionPhase::WORKING || phase == SessionPhase::STREAMING;
        const bool has_stream = session.stream.owner != nullptr;
        const bool is_usable = is_working && has_stream;
        if (!is_usable) { return SessionResult::failure(KIOTTY_NET_CODE_BUSY); }
        return found;
    }

    namespace
    {
        size_t openStream(HttpSession& session)
        {
            if (session.phase == SessionPhase::STREAMING) { return 0; }
            sealResponse(session);
            session.phase = SessionPhase::STREAMING;
            return headerBytesOf(session);
        }

        KiottyNetCode sendChunk(HttpSession& session,
                                Span<const char> data,
                                size_t kept,
                                const Deadline& deadline)
        {
            const size_t limit = session.host.send_capacity;
            const Span<const char> framed = session.response.frame().frameChunk(data, kept, limit);
            if (framed.empty()) { return sendPieces(session, data, kept, deadline); }
            return sendSpan(session, framed, deadline);
        }

        KiottyNetCode sendPieces(HttpSession& session,
                                 Span<const char> data,
                                 size_t kept,
                                 const Deadline& deadline)
        {
            const ChunkHead head = chunkHeadOf(data.length);
            const Span<const char> pieces[] = {
                session.response.frame().chunkAt(0, kept),
                head.span(),
                data,
                Span<const char>{ LINE_END, sizeof(LINE_END) - 1 },
            };
            for (const Span<const char>& piece : pieces)
            {
                const KiottyNetCode sent = sendSpan(session, piece, deadline);
                if (sent != KIOTTY_NET_CODE_SUCCESS) { return sent; }
            }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode sendHeaderOnly(HttpSession& session, size_t kept, const Deadline& deadline)
        {
            const Span<const char> header = session.response.frame().chunkAt(0, kept);
            const KiottyNetCode sent = sendSpan(session, header, deadline);
            if (sent != KIOTTY_NET_CODE_SUCCESS) { return sent; }
            return KIOTTY_NET_CODE_CLOSED;
        }

        KiottyNetCode sendSpan(const HttpSession& session,
                               Span<const char> data,
                               const Deadline& deadline)
        {
            if (data.empty()) { return KIOTTY_NET_CODE_SUCCESS; }
            if (deadline.expired()) { return KIOTTY_NET_CODE_TIMEOUT; }
            const uint32_t left = deadline.remaining();
            const bool is_short = deadline.limited() && left < SHORTEST_WAIT_MILLISECONDS;
            const uint32_t wait = is_short ? SHORTEST_WAIT_MILLISECONDS : left;
            const KiottyNetSpan bytes{ data.data, data.length };
            const KiottyNetConnectionStream stream = session.stream;
            return stream.owner->writeStream(stream.id, bytes, wait).code;
        }

        KiottyNetCode failureOf(const HttpSession& session, KiottyNetCode code)
        {
            const bool is_peer_closed = code == KIOTTY_NET_CODE_PEER_CLOSED;
            const bool is_lost = is_peer_closed || code == KIOTTY_NET_CODE_SOCKET_ERROR;
            if (!is_lost) { return code; }
            if (session.host.is_closing.load()) { return KIOTTY_NET_CODE_CLOSED; }
            const KiottyNetConnectionStream stream = session.stream;
            const bool is_open = stream.owner->isStreamOpen(stream.id);
            return is_open ? code : KIOTTY_NET_CODE_PEER_CLOSED;
        }

        KiottyNetCode handOver(HttpSession& session,
                               KiottyNetConnectionStream stream,
                               const KiottyNetConnectionAction& then)
        {
            HttpSessionPool& sessions = session.host.sessions;
            const bool is_orphaned = sessions.handBack(session);
            const KiottyNetCode closed = stream.owner->closeStream(stream.id, then);
            if (is_orphaned) { sessions.release(session); }
            return closed;
        }

        KiottyNetConnectionAction finishingActionOf(HttpSession& session, SessionPhase phase)
        {
            if (phase == SessionPhase::WORKING) { return sealedPieceOf(session); }
            const KiottyNetCode failure = session.stream_failure;
            if (failure == KIOTTY_NET_CODE_SUCCESS)
            {
                return firstPieceOf(session, headerBytesOf(session));
            }
            const bool has_body = session.response.frame().hasSendableBody();
            const bool is_ended = failure == KIOTTY_NET_CODE_CLOSED && !has_body;
            if (is_ended) { return nextExchangeOf(session); }
            return thenClose(failure);
        }

        KiottyNetConnectionAction sealedPieceOf(HttpSession& session)
        {
            sealResponse(session);
            return firstPieceOf(session, 0);
        }

        KiottyNetConnectionAction firstPieceOf(HttpSession& session, size_t sent)
        {
            session.sent = sent;
            const size_t capacity = session.host.send_capacity;
            const Span<const char> chunk = session.response.frame().chunkAt(sent, capacity);
            return thenWrite(KiottyNetSpan{ chunk.data, chunk.length }, 0);
        }

        KiottyNetConnectionAction nextExchangeOf(HttpSession& session)
        {
            const AfterResponse after = afterResponseOf(session.persistence);
            if (after == AfterResponse::CLOSE) { return thenClose(KIOTTY_NET_CODE_SUCCESS); }
            const KiottyNetConnectionHandle connection = session.request.request().connection;
            const uint32_t wait = session.host.limits.keep_alive_timeout_milliseconds;
            return beginExchange(session, connection, wait).entry_action;
        }

        size_t headerBytesOf(const HttpSession& session)
        {
            return session.response.frame().chunkAt(0, WHOLE).length;
        }
    }
}
