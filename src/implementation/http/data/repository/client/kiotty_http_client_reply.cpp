#include "http/data/repository/client/kiotty_http_client_reply.h"

#include "core/kiotty_codes.h"
#include "http/data/repository/client/kiotty_http_client_send.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_INTERIM_REPLIES = 16;

        LineResult      nextLine(StreamClient& line, const Deadline& deadline);
        ReplyHead       receiveHead(StreamClient& line,
                                    HttpReplyStore& reply,
                                    const Deadline& deadline);
        size_t          stagedBytesOf(StreamClient& line);
        void            startBody(StreamClient& line,
                                  HttpClientState& state,
                                  const ReplyFraming& framing);
        void            finishBody(StreamClient& line, HttpClientState& state);
        KiottyNetResult endInput(StreamClient& line, HttpClientState& state, KiottyNetCode code);
    }

    ReplyHead receiveReply(StreamClient& line,
                           HttpClientState& state,
                           KiottyNetHttpMethod method,
                           const Deadline& deadline)
    {
        bool saw_bytes = false;
        for (size_t interim = 0; interim < MAX_INTERIM_REPLIES; ++interim)
        {
            const ReplyHead head = receiveHead(line, state.reply, deadline);
            saw_bytes = saw_bytes || head.saw_bytes;
            if (head.code != KIOTTY_NET_CODE_SUCCESS) { return ReplyHead{ head.code, saw_bytes }; }
            const ReplyFramingResult framing = state.reply.finish(method);
            if (!framing) { return ReplyHead{ framing.code, true }; }
            if (framing.value.phase == ReplyPhase::FINAL)
            {
                startBody(line, state, framing.value);
                return ReplyHead{ KIOTTY_NET_CODE_SUCCESS, true };
            }
        }
        return ReplyHead{ KIOTTY_NET_CODE_PROTOCOL_ERROR, true };
    }

    KiottyNetResult pumpBody(StreamClient& line,
                             HttpClientState& state,
                             Span<char> buffer,
                             const Deadline& deadline)
    {
        for (;;)
        {
            const TimeLeftResult left = timeLeftOf(deadline);
            if (!left) { return resultOf(0, left.code); }
            const StreamResult stream = line.stream();
            if (!stream) { return endInput(line, state, stream.code); }
            const StagedResult staged = stream.value().peek(1, left.value);
            if (!staged) { return endInput(line, state, staged.code); }
            const BodyStep step = state.body.take(staged.value, buffer);
            stream.value().consume(step.consumed);
            if (step.code != KIOTTY_NET_CODE_SUCCESS)
            {
                line.disconnect();
                return resultOf(0, step.code);
            }
            if (step.is_finished) { finishBody(line, state); }
            const bool is_answered = step.produced > 0 || step.is_finished;
            if (is_answered) { return resultOf(step.produced, step.code); }
        }
    }

    KiottyNetHttpReply replyOf(StreamClient& line, HttpClientState& state, KiottyNetCode code)
    {
        if (code != KIOTTY_NET_CODE_SUCCESS)
        {
            line.disconnect();
            return KiottyNetHttpReply{ code, 0, nullptr, 0 };
        }
        const Span<const KiottyNetHttpHeader> headers = state.reply.headers();
        return KiottyNetHttpReply{ code, state.reply.status(), headers.data, headers.length };
    }

    bool hasStagedReply(StreamClient& line)
    {
        return stagedBytesOf(line) > 0;
    }

    ReplyPhaseResult receiveOneReply(StreamClient& line,
                                     HttpClientState& state,
                                     KiottyNetHttpMethod method,
                                     const Deadline& deadline)
    {
        const ReplyHead head = receiveHead(line, state.reply, deadline);
        if (head.code != KIOTTY_NET_CODE_SUCCESS) { return ReplyPhaseResult::failure(head.code); }
        const ReplyFramingResult framing = state.reply.finish(method);
        if (!framing) { return ReplyPhaseResult::failure(framing.code); }
        const bool is_final = framing.value.phase == ReplyPhase::FINAL;
        if (is_final) { startBody(line, state, framing.value); }
        return ReplyPhaseResult::success(framing.value.phase);
    }

    void dropUnreadReply(StreamClient& line, HttpClientState& state)
    {
        if (!state.body.isFinished()) { line.disconnect(); }
        dropReply(state);
    }

    namespace
    {
        LineResult nextLine(StreamClient& line, const Deadline& deadline)
        {
            const TimeLeftResult left = timeLeftOf(deadline);
            if (!left) { return LineResult::failure(left.code); }
            const StreamResult stream = line.stream();
            if (!stream) { return LineResult::failure(stream.code); }
            return readLine(stream.value(), left.value);
        }

        ReplyHead receiveHead(StreamClient& line, HttpReplyStore& reply, const Deadline& deadline)
        {
            reply.begin();
            const LineResult status = nextLine(line, deadline);
            if (!status) { return ReplyHead{ status.code, stagedBytesOf(line) > 0 }; }
            const KiottyNetCode parsed = reply.takeStatusLine(status.value);
            if (parsed != KIOTTY_NET_CODE_SUCCESS) { return ReplyHead{ parsed, true }; }
            for (;;)
            {
                const LineResult header = nextLine(line, deadline);
                if (!header) { return ReplyHead{ header.code, true }; }
                if (header.value.empty()) { return ReplyHead{ KIOTTY_NET_CODE_SUCCESS, true }; }
                const KiottyNetCode stored = reply.takeHeader(header.value);
                if (stored != KIOTTY_NET_CODE_SUCCESS) { return ReplyHead{ stored, true }; }
            }
        }

        size_t stagedBytesOf(StreamClient& line)
        {
            const StreamResult stream = line.stream();
            if (!stream) { return 0; }
            const StagedResult staged = stream.value().peek(0, 0);
            return staged ? staged.value.length : 0;
        }

        void startBody(StreamClient& line, HttpClientState& state, const ReplyFraming& framing)
        {
            state.body = HttpBodyReader(framing);
            state.persistence = framing.persistence;
            if (state.body.isFinished()) { finishBody(line, state); }
        }

        void finishBody(StreamClient& line, HttpClientState& state)
        {
            const bool is_kept = state.persistence == Persistence::KEEP_ALIVE;
            if (!is_kept) { line.disconnect(); }
        }

        KiottyNetResult endInput(StreamClient& line, HttpClientState& state, KiottyNetCode code)
        {
            if (code == KIOTTY_NET_CODE_TIMEOUT) { return resultOf(0, code); }
            const bool is_closed_by_peer = code == KIOTTY_NET_CODE_PEER_CLOSED;
            const KiottyNetCode ended = is_closed_by_peer ? state.body.closeInput() : code;
            line.disconnect();
            return resultOf(0, ended);
        }
    }
}
