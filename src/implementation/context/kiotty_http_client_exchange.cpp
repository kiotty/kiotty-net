#include "context/kiotty_http_client_exchange.h"

#include "context/kiotty_http_client.h"
#include "core/kiotty_busy_guard.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_request_check.h"
#include "domain/usecases/http/kiotty_http_response_text.h"
#include "domain/usecases/http/kiotty_http_upload.h"

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

    HttpClientExchange::HttpClientExchange(HttpClient& client)
        : _client(client)
    {
    }

    KiottyNetHttpReply HttpClientExchange::send(const ClientRequest& request,
                                                uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_client._busy);
        if (!guard.isHeld()) { return failedReply(KIOTTY_NET_CODE_BUSY); }
        _client.settleStop();
        dropUpload();
        return run(request, timeout_milliseconds);
    }

    KiottyNetResult HttpClientExchange::readBody(Span<char> buffer, uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_client._busy);
        if (!guard.isHeld()) { return resultOf(0, KIOTTY_NET_CODE_BUSY); }
        _client.settleStop();
        settleUpload();
        if (_client._upload.mode != UploadMode::IDLE) { return resultOf(0, INVALID); }
        const Span<char> target = cappedOf(buffer);
        const BodyStep idle = _client._body.take(Span<const char>{ nullptr, 0 }, target);
        const bool is_settled = idle.code != KIOTTY_NET_CODE_SUCCESS || idle.is_finished;
        if (is_settled) { return resultOf(0, idle.code); }
        const Deadline deadline(timeout_milliseconds);
        return _client.pumpBody(target, deadline);
    }

    KiottyNetCode HttpClientExchange::begin(const ClientRequest& request,
                                            uint64_t body_length,
                                            uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_client._busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        _client.settleStop();
        const KiottyNetCode checked = checkStart(request);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const HeadLengthResult head = _client.writeHead(request, body_length);
        if (!head) { return head.code; }
        dropUpload();
        _client.dropUnreadReply();
        const Deadline deadline(timeout_milliseconds);
        const Span<const char> no_body{ nullptr, 0 };
        const KiottyNetCode sent = _client.transmit(head.value, no_body, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS)
        {
            _client.disconnect();
            return sent;
        }
        _client._upload = startUpload(request.method, body_length);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetResult HttpClientExchange::write(Span<const char> data, uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_client._busy);
        if (!guard.isHeld()) { return resultOf(0, KIOTTY_NET_CODE_BUSY); }
        _client.settleStop();
        settleUpload();
        const KiottyNetCode checked = checkUploadPiece(_client._upload, data.length);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return resultOf(0, checked); }
        if (data.empty()) { return resultOf(0, KIOTTY_NET_CODE_SUCCESS); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode sent = sendPiece(data, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS)
        {
            dropUpload();
            return resultOf(0, sent);
        }
        _client._upload = takeUploadPiece(_client._upload, data.length);
        return resultOf(data.length, KIOTTY_NET_CODE_SUCCESS);
    }

    KiottyNetHttpReply HttpClientExchange::finish(uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_client._busy);
        if (!guard.isHeld()) { return failedReply(KIOTTY_NET_CODE_BUSY); }
        _client.settleStop();
        settleUpload();
        if (_client._upload.mode == UploadMode::IDLE) { return failedReply(INVALID); }
        const KiottyNetHttpMethod method = _client._upload.method;
        const KiottyNetCode ended = checkUploadEnd(_client._upload);
        if (ended != KIOTTY_NET_CODE_SUCCESS)
        {
            dropUpload();
            return failedReply(ended);
        }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode sent = sendEnd(deadline);
        _client._upload = idleUpload();
        const bool is_sent = sent == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode received = is_sent ? _client.receiveReply(method, deadline) : sent;
        return _client.replyOf(received);
    }

    KiottyNetHttpReply HttpClientExchange::run(const ClientRequest& request,
                                               uint32_t timeout_milliseconds)
    {
        if (!_client._has_target) { return failedReply(INVALID); }
        const KiottyNetCode body = checkRequestBody(request.type, request.body);
        if (body != KIOTTY_NET_CODE_SUCCESS) { return failedReply(body); }
        const HeadLengthResult head = _client.writeHead(request, request.body.length);
        if (!head) { return failedReply(head.code); }
        const Deadline deadline(timeout_milliseconds);
        const Attempt first = attempt(request, head.value, deadline);
        const KiottyNetCode received = first.is_stale ? resend(request, deadline) : first.code;
        return _client.replyOf(received);
    }

    HttpClientExchange::Attempt HttpClientExchange::attempt(const ClientRequest& request,
                                                            size_t head_length,
                                                            const Deadline& deadline)
    {
        _client.dropUnreadReply();
        const bool is_reused = _client.isConnected();
        const KiottyNetCode sent = _client.transmit(head_length, request.body, deadline);
        const bool is_sent = sent == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode received =
            is_sent ? _client.receiveReply(request.method, deadline) : sent;
        const bool is_unanswered = !_client._has_reply_bytes && _client.stagedBytes() == 0;
        const bool is_stale = is_reused && is_unanswered && isConnectionLoss(received);
        return Attempt{ received, is_stale };
    }

    KiottyNetCode HttpClientExchange::resend(const ClientRequest& request,
                                             const Deadline& deadline)
    {
        _client.disconnect();
        if (!isIdempotent(request.method)) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        const HeadLengthResult head = _client.writeHead(request, request.body.length);
        if (!head) { return head.code; }
        return attempt(request, head.value, deadline).code;
    }

    KiottyNetCode HttpClientExchange::checkStart(const ClientRequest& request)
    {
        if (!_client._has_target) { return INVALID; }
        if (request.body.length > 0) { return INVALID; }
        return checkRequestBody(request.type, request.body);
    }

    KiottyNetCode HttpClientExchange::sendPiece(Span<const char> data, const Deadline& deadline)
    {
        const bool is_counted = _client._upload.mode == UploadMode::LENGTH;
        if (is_counted) { return _client.writeAll(data, deadline); }
        const ChunkHead head = chunkHeadOf(data.length);
        const KiottyNetCode opened = _client.writeAll(head.span(), deadline);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        const KiottyNetCode carried = _client.writeAll(data, deadline);
        if (carried != KIOTTY_NET_CODE_SUCCESS) { return carried; }
        return _client.writeAll(textOf(LINE_END), deadline);
    }

    KiottyNetCode HttpClientExchange::sendEnd(const Deadline& deadline)
    {
        const bool is_counted = _client._upload.mode == UploadMode::LENGTH;
        if (is_counted) { return KIOTTY_NET_CODE_SUCCESS; }
        return _client.writeAll(textOf(LAST_UPLOAD_CHUNK), deadline);
    }

    void HttpClientExchange::settleUpload()
    {
        const bool is_open = _client.isConnected();
        if (!is_open) { _client._upload = idleUpload(); }
    }

    void HttpClientExchange::dropUpload()
    {
        const bool is_uploading = _client._upload.mode != UploadMode::IDLE;
        if (is_uploading) { _client.disconnect(); }
        _client._upload = idleUpload();
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
