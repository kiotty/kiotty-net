#include "http/data/repository/client/kiotty_http_client_exchange.h"

#include "core/kiotty_busy_guard.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_upload_rules.h"
#include "http/data/repository/client/kiotty_http_client_reply.h"
#include "http/data/repository/client/kiotty_http_client_send.h"
#include "http/data/repository/request/kiotty_http_request_check_rules.h"
#include "http/data/repository/response/kiotty_http_response_text_builder.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char LAST_UPLOAD_CHUNK[] = "0\r\n\r\n";

        KiottyNetHttpReply failedReply(KiottyNetCode code);
        Span<char>         cappedOf(Span<char> buffer);
        bool               isIdempotent(KiottyNetHttpMethod method);
        bool               isConnectionLoss(KiottyNetCode code);
    }

    HttpClientExchange::HttpClientExchange(HttpClientState& state, StreamClient& line)
        : _state(state)
        , _line(line)
    {
    }

    KiottyNetHttpReply HttpClientExchange::send(const ClientRequest& request,
                                                uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return failedReply(KIOTTY_NET_CODE_BUSY); }
        settleStop(_line);
        dropUpload();
        return run(request, timeout_milliseconds);
    }

    KiottyNetResult HttpClientExchange::readBody(Span<char> buffer, uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return resultOf(0, KIOTTY_NET_CODE_BUSY); }
        settleUpload();
        if (_state.upload.mode != UploadMode::IDLE) { return resultOf(0, INVALID); }
        const Span<char> target = cappedOf(buffer);
        const BodyStep idle = _state.body.take(Span<const char>{ nullptr, 0 }, target);
        const bool is_settled = idle.code != KIOTTY_NET_CODE_SUCCESS || idle.is_finished;
        if (is_settled) { return resultOf(0, idle.code); }
        const Deadline deadline(timeout_milliseconds);
        return pumpBody(_line, _state, target, deadline);
    }

    KiottyNetCode HttpClientExchange::begin(const ClientRequest& request,
                                            uint64_t body_length,
                                            uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        const Deadline deadline(timeout_milliseconds);
        return start(request, body_length, deadline);
    }

    KiottyNetHttpReply HttpClientExchange::finish(uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return failedReply(KIOTTY_NET_CODE_BUSY); }
        const Deadline deadline(timeout_milliseconds);
        return end(deadline);
    }

    KiottyNetCode HttpClientExchange::start(const ClientRequest& request,
                                            uint64_t body_length,
                                            const Deadline& deadline)
    {
        settleStop(_line);
        const KiottyNetCode checked = checkStart(request);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const HeadBytesResult head = writeHead(_state, request, body_length);
        if (!head) { return head.code; }
        dropUpload();
        dropUnreadReply(_line, _state);
        const KiottyNetCode sent = sendHead(head.value, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS)
        {
            _line.disconnect();
            return sent;
        }
        _state.upload = startUpload(request.method, body_length);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult HttpClientExchange::write(Span<const char> data, uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return resultOf(0, KIOTTY_NET_CODE_BUSY); }
        settleUpload();
        const KiottyNetCode checked = checkUploadPiece(_state.upload, data.length);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, checked); }
        if (data.empty()) { return resultOf(0, KIOTTY_NET_CODE_SUCCESS); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode sent = sendPiece(data, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS)
        {
            stopUpload(sent);
            return resultOf(0, sent);
        }
        _state.upload = takeUploadPiece(_state.upload, data.length);
        return resultOf(data.length, KIOTTY_NET_CODE_SUCCESS);
    }

    KiottyNetHttpReply HttpClientExchange::end(const Deadline& deadline)
    {
        settleUpload();
        if (_state.upload.mode == UploadMode::IDLE) { return failedReply(INVALID); }
        const KiottyNetHttpMethod method = _state.upload.method;
        const bool is_refused = _state.upload.mode == UploadMode::REFUSED;
        const bool is_stopped = is_refused || _state.upload.mode == UploadMode::ANSWERED;
        if (is_stopped) { return stoppedReply(deadline); }
        const KiottyNetCode ended = checkUploadEnd(_state.upload);
        if (ended != KIOTTY_NET_CODE_SUCCESS)
        {
            dropUpload();
            return failedReply(ended);
        }
        const KiottyNetCode sent = sendEnd(deadline);
        _state.upload = idleUpload();
        const bool is_sent = sent == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode received =
            is_sent ? receiveReply(_line, _state, method, deadline).code : sent;
        return replyOf(_line, _state, received);
    }

    KiottyNetHttpReply HttpClientExchange::run(const ClientRequest& request,
                                               uint32_t timeout_milliseconds)
    {
        if (!_state.target.is_set) { return failedReply(INVALID); }
        const KiottyNetCode body = checkRequestBody(request.type, request.body);
        if (body != KIOTTY_NET_CODE_SUCCESS) { return failedReply(body); }
        const HeadBytesResult head = writeHead(_state, request, request.body.length);
        if (!head) { return failedReply(head.code); }
        const Deadline deadline(timeout_milliseconds);
        const Attempt first = attempt(request, head.value, deadline);
        const KiottyNetCode received = first.is_stale ? resend(request, deadline) : first.code;
        return replyOf(_line, _state, received);
    }

    HttpClientExchange::Attempt HttpClientExchange::attempt(const ClientRequest& request,
                                                            Span<const char> head,
                                                            const Deadline& deadline)
    {
        dropUnreadReply(_line, _state);
        const Connected link = ensureConnected(_line, _state.target, deadline);
        const KiottyNetCode sent = transmit(head, request.body, link, deadline);
        const bool is_sent = sent == KIOTTY_NET_CODE_SUCCESS;
        const ReplyHead unsent{ sent, false };
        const ReplyHead reply =
            is_sent ? receiveReply(_line, _state, request.method, deadline) : unsent;
        const bool is_stale = link.is_reused && !reply.saw_bytes && isConnectionLoss(reply.code);
        return Attempt{ reply.code, is_stale };
    }

    KiottyNetCode HttpClientExchange::resend(const ClientRequest& request,
                                             const Deadline& deadline)
    {
        _line.disconnect();
        if (!isIdempotent(request.method)) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        const HeadBytesResult head = writeHead(_state, request, request.body.length);
        if (!head) { return head.code; }
        return attempt(request, head.value, deadline).code;
    }

    KiottyNetCode HttpClientExchange::transmit(Span<const char> head,
                                               Span<const char> body,
                                               const Connected& link,
                                               const Deadline& deadline)
    {
        const bool is_connected = link.code == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode sent =
            is_connected ? sendBytes(_line, head, body, deadline) : link.code;
        _state.head_area.fill('\0');
        return sent;
    }

    KiottyNetCode HttpClientExchange::checkStart(const ClientRequest& request)
    {
        if (!_state.target.is_set) { return INVALID; }
        if (request.body.length > 0) { return INVALID; }
        return checkRequestBody(request.type, request.body);
    }

    KiottyNetCode HttpClientExchange::sendHead(Span<const char> head, const Deadline& deadline)
    {
        Connected link = ensureIdleConnected(_line, _state.target, deadline);
        const bool is_reused = link.code == KIOTTY_NET_CODE_SUCCESS && link.is_reused;
        const Span<const char> nothing{ head.data, 0 };
        const KiottyNetCode probed =
            is_reused ? writeOnce(_line, nothing, deadline).code : KIOTTY_NET_CODE_SUCCESS;
        if (probed != KIOTTY_NET_CODE_SUCCESS)
        {
            _line.disconnect();
            link = ensureConnected(_line, _state.target, deadline);
        }
        const Span<const char> no_body{ nullptr, 0 };
        return transmit(head, no_body, link, deadline);
    }

    KiottyNetCode HttpClientExchange::sendPiece(Span<const char> data, const Deadline& deadline)
    {
        const bool is_counted = _state.upload.mode == UploadMode::LENGTH;
        if (is_counted) { return sendUploadBytes(data, deadline); }
        const ChunkHead head = chunkHeadOf(data.length);
        const KiottyNetCode opened = sendUploadBytes(head.span(), deadline);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        const KiottyNetCode carried = sendUploadBytes(data, deadline);
        if (carried != KIOTTY_NET_CODE_SUCCESS) { return carried; }
        return sendUploadBytes(textOf(LINE_END), deadline);
    }

    KiottyNetCode HttpClientExchange::sendUploadBytes(Span<const char> data,
                                                      const Deadline& deadline)
    {
        size_t done = 0;
        for (;;)
        {
            const KiottyNetCode early = takeEarlyReplies(deadline);
            if (early != KIOTTY_NET_CODE_SUCCESS) { return early; }
            const KiottyNetResult sent = writeOnce(_line, data.skip(done), deadline);
            done += static_cast<size_t>(sent.bytes);
            if (sent.code != KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING) { return sent.code; }
        }
    }

    KiottyNetCode HttpClientExchange::takeEarlyReplies(const Deadline& deadline)
    {
        while (hasStagedReply(_line))
        {
            const ReplyPhaseResult early =
                receiveOneReply(_line, _state, _state.upload.method, deadline);
            if (!early) { return early.code; }
            const bool is_final = early.value == ReplyPhase::FINAL;
            if (is_final) { return KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING; }
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpClientExchange::sendEnd(const Deadline& deadline)
    {
        const bool is_counted = _state.upload.mode == UploadMode::LENGTH;
        if (is_counted) { return KIOTTY_NET_CODE_SUCCESS; }
        return writeAll(_line, textOf(LAST_UPLOAD_CHUNK), deadline);
    }

    KiottyNetHttpReply HttpClientExchange::stoppedReply(const Deadline& deadline)
    {
        const HttpUpload stopped = _state.upload;
        _state.upload = idleUpload();
        const bool is_answered = stopped.mode == UploadMode::ANSWERED;
        if (is_answered) { return closingReplyOf(KIOTTY_NET_CODE_SUCCESS); }
        const ReplyHead reply = receiveReply(_line, _state, stopped.method, deadline);
        return closingReplyOf(reply.code);
    }

    KiottyNetHttpReply HttpClientExchange::closingReplyOf(KiottyNetCode code)
    {
        const bool is_answered = code == KIOTTY_NET_CODE_SUCCESS;
        if (is_answered) { _state.persistence = Persistence::CLOSE; }
        const bool is_read = is_answered && _state.body.isFinished();
        if (is_read) { _line.disconnect(); }
        return replyOf(_line, _state, code);
    }

    void HttpClientExchange::settleUpload()
    {
        const bool is_stopped = _line.stop().accept();
        if (is_stopped) { _line.disconnect(); }
        const bool is_open = static_cast<bool>(_line.stream());
        const bool is_kept = is_open || _state.upload.mode == UploadMode::ANSWERED;
        if (is_stopped || !is_kept) { _state.upload = idleUpload(); }
    }

    void HttpClientExchange::dropUpload()
    {
        const bool is_uploading = _state.upload.mode != UploadMode::IDLE;
        if (is_uploading) { _line.disconnect(); }
        _state.upload = idleUpload();
    }

    void HttpClientExchange::stopUpload(KiottyNetCode code)
    {
        if (code == KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING)
        {
            _state.upload = answerUpload(_state.upload);
            return;
        }
        if (isConnectionLoss(code))
        {
            _state.upload = refuseUpload(_state.upload);
            return;
        }
        dropUpload();
    }

    namespace
    {
        KiottyNetHttpReply failedReply(KiottyNetCode code)
        {
            return KiottyNetHttpReply{ code, 0, nullptr, 0 };
        }

        Span<char> cappedOf(Span<char> buffer)
        {
            const bool is_large = buffer.length > RESULT_MAXIMUM_BYTES;
            return Span<char>{ buffer.data, is_large ? RESULT_MAXIMUM_BYTES : buffer.length };
        }

        bool isIdempotent(KiottyNetHttpMethod method)
        {
            const bool is_reading = method == KIOTTY_NET_HTTP_METHOD_GET
                                    || method == KIOTTY_NET_HTTP_METHOD_HEAD
                                    || method == KIOTTY_NET_HTTP_METHOD_OPTIONS;
            const bool is_replacing = method == KIOTTY_NET_HTTP_METHOD_PUT
                                      || method == KIOTTY_NET_HTTP_METHOD_DELETE;
            return is_reading || is_replacing;
        }

        bool isConnectionLoss(KiottyNetCode code)
        {
            return code == KIOTTY_NET_CODE_PEER_CLOSED || code == KIOTTY_NET_CODE_SOCKET_ERROR;
        }
    }
}
