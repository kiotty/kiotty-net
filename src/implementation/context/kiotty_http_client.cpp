#include "context/kiotty_http_client.h"

#include "context/kiotty_http_configuration.h"
#include "core/kiotty_busy_guard.h"
#include "core/kiotty_result.h"
#include "domain/usecases/http/kiotty_http_request_check.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using TimeLeftResult = Result<uint32_t, KiottyNetCode>;

        constexpr size_t HEAD_AREA_COUNT = 2;
        constexpr size_t MAX_INTERIM_REPLIES = 16;
        constexpr size_t RESULT_MAXIMUM_BYTES = 0x7FFFFFFF;

        TimeLeftResult     timeLeftOf(const Deadline& deadline);
        KiottyNetHttpReply failedReply(KiottyNetCode code);
        KiottyNetResult    resultOf(size_t bytes, KiottyNetCode code);
        KiottyNetCode      checkLimits(const KiottyNetHttpLimits& limits);
        uint16_t           defaultPortOf(const KiottyNetTlsClientConfiguration* tls);
        Span<char>         cappedOf(Span<char> buffer);
    }

    HttpClient::HttpClient()
        : StreamClient()
        , _head_bytes()
        , _reply_bytes()
        , _shared_entries()
        , _reply_entries()
        , _shared(Span<char>{ nullptr, 0 }, Span<KiottyNetHttpHeader>{ nullptr, 0 })
        , _reply(Span<char>{ nullptr, 0 }, Span<KiottyNetHttpHeader>{ nullptr, 0 })
        , _body()
        , _address()
        , _host()
        , _max_headers(0)
        , _persistence(Persistence::CLOSE)
        , _has_target(false)
        , _is_busy(false)
    {
    }

    HttpClient::~HttpClient() = default;

    KiottyNetCode HttpClient::initialize(PoolHandle id,
                                         const HttpClientConfiguration& configuration)
    {
        const KiottyNetHttpLimits& limits = configuration.limits;
        const KiottyNetCode checked = checkLimits(limits);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const KiottyNetCode base = StreamClient::initialize(id, configuration.client);
        if (base != KIOTTY_NET_CODE_SUCCESS) { return base; }
        _head_bytes.assign(HEAD_AREA_COUNT * limits.max_header_bytes, '\0');
        _reply_bytes.assign(limits.max_header_bytes, '\0');
        _shared_entries.assign(limits.max_headers, KiottyNetHttpHeader{});
        _reply_entries.assign(limits.max_headers, KiottyNetHttpHeader{});
        const Span<KiottyNetHttpHeader> shared{ _shared_entries.data(), _shared_entries.size() };
        const Span<KiottyNetHttpHeader> reply{ _reply_entries.data(), _reply_entries.size() };
        _shared = HttpSharedHeaders(sharedArea(), shared);
        _reply = HttpReplyStore(Span<char>{ _reply_bytes.data(), _reply_bytes.size() }, reply);
        _body = HttpBodyReader();
        _max_headers = limits.max_headers;
        _has_target = false;
        _is_busy.store(false);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpClient::reset() noexcept
    {
        StreamClient::reset();
        _shared.clear();
        requestArea().fill('\0');
        dropReply();
        _has_target = false;
    }

    KiottyNetCode HttpClient::connect(const KiottyNetAddress& address,
                                      const KiottyNetTlsClientConfiguration* tls)
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        dropReply();
        _has_target = false;
        const KiottyNetCode prepared = prepare(address, tls);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        const HostFieldResult host = hostFieldOf(address, defaultPortOf(tls));
        if (!host) { return host.code; }
        _address = address;
        _host = host.value;
        _has_target = true;
        return reconnect(_address);
    }

    void HttpClient::disconnect()
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return; }
        StreamClient::disconnect();
    }

    KiottyNetHttpReply HttpClient::send(const ClientRequest& request,
                                        uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return failedReply(KIOTTY_NET_CODE_BUSY); }
        return exchange(request, timeout_milliseconds);
    }

    KiottyNetResult HttpClient::readBody(Span<char> buffer, uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_is_busy);
        if (!guard.isHeld()) { return resultOf(0, KIOTTY_NET_CODE_BUSY); }
        const Span<char> target = cappedOf(buffer);
        const BodyStep idle = _body.take(Span<const char>{ nullptr, 0 }, target);
        const bool is_settled = idle.code != KIOTTY_NET_CODE_SUCCESS || idle.is_finished;
        if (is_settled) { return resultOf(0, idle.code); }
        const Deadline deadline(timeout_milliseconds);
        return pumpBody(target, deadline);
    }

    HttpClientHeaders HttpClient::headers()
    {
        return HttpClientHeaders(_shared, _is_busy);
    }

    KiottyNetHttpReply HttpClient::exchange(const ClientRequest& request,
                                            uint32_t timeout_milliseconds)
    {
        if (!_has_target) { return failedReply(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        const KiottyNetCode body = checkRequestBody(request.type, request.body);
        if (body != KIOTTY_NET_CODE_SUCCESS) { return failedReply(body); }
        const HeadLengthResult head = writeHead(request);
        if (!head) { return failedReply(head.code); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode sent = transmit(head.value, request.body, deadline);
        const bool is_sent = sent == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode received = is_sent ? receiveReply(request.method, deadline) : sent;
        if (received != KIOTTY_NET_CODE_SUCCESS)
        {
            StreamClient::disconnect();
            return failedReply(received);
        }
        const Span<const KiottyNetHttpHeader> headers = _reply.headers();
        return KiottyNetHttpReply{ received, _reply.status(), headers.data, headers.length };
    }

    HeadLengthResult HttpClient::writeHead(const ClientRequest& request)
    {
        const RequestHead head{ _shared.list(), request.headers,      request.path,
                                _host.span(),   request.body.length, _max_headers,
                                request.method, request.type };
        const HeadLengthResult written = writeRequestHead(requestArea(), head);
        if (!written) { requestArea().fill('\0'); }
        return written;
    }

    KiottyNetCode HttpClient::transmit(size_t head_length,
                                       Span<const char> body,
                                       const Deadline& deadline)
    {
        if (!_body.isFinished()) { StreamClient::disconnect(); }
        dropReply();
        const bool is_reused = isConnected();
        const KiottyNetCode connected = is_reused ? KIOTTY_NET_CODE_SUCCESS : reconnect(_address);
        const Span<const char> head{ requestArea().data, head_length };
        const bool is_connected = connected == KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetCode sent = is_connected ? writeAll(head, deadline) : connected;
        requestArea().fill('\0');
        if (sent != KIOTTY_NET_CODE_SUCCESS || body.empty()) { return sent; }
        return writeAll(body, deadline);
    }

    KiottyNetCode HttpClient::writeAll(Span<const char> data, const Deadline& deadline)
    {
        const TimeLeftResult left = timeLeftOf(deadline);
        if (!left) { return left.code; }
        const KiottyNetSpan bytes{ data.data, data.length };
        return StreamClient::write(bytes, left.value).code;
    }

    KiottyNetCode HttpClient::receiveReply(KiottyNetHttpMethod method, const Deadline& deadline)
    {
        for (size_t interim = 0; interim < MAX_INTERIM_REPLIES; ++interim)
        {
            const KiottyNetCode head = receiveHead(deadline);
            if (head != KIOTTY_NET_CODE_SUCCESS) { return head; }
            const ReplyFramingResult framing = _reply.finish(method);
            if (!framing) { return framing.code; }
            if (framing.value.phase == ReplyPhase::FINAL)
            {
                startBody(framing.value);
                return KIOTTY_NET_CODE_SUCCESS;
            }
        }
        return KIOTTY_NET_CODE_PROTOCOL_ERROR;
    }

    KiottyNetCode HttpClient::receiveHead(const Deadline& deadline)
    {
        _reply.begin();
        const LineResult status = nextLine(deadline);
        if (!status) { return status.code; }
        const KiottyNetCode parsed = _reply.takeStatusLine(status.value);
        if (parsed != KIOTTY_NET_CODE_SUCCESS) { return parsed; }
        for (;;)
        {
            const LineResult line = nextLine(deadline);
            if (!line) { return line.code; }
            if (line.value.empty()) { return KIOTTY_NET_CODE_SUCCESS; }
            const KiottyNetCode stored = _reply.takeHeader(line.value);
            if (stored != KIOTTY_NET_CODE_SUCCESS) { return stored; }
        }
    }

    LineResult HttpClient::nextLine(const Deadline& deadline)
    {
        const TimeLeftResult left = timeLeftOf(deadline);
        if (!left) { return LineResult::failure(left.code); }
        return readLine(left.value);
    }

    void HttpClient::startBody(const ReplyFraming& framing)
    {
        _body = HttpBodyReader(framing);
        _persistence = framing.persistence;
        if (_body.isFinished()) { finishBody(); }
    }

    void HttpClient::finishBody()
    {
        const bool is_kept = _persistence == Persistence::KEEP_ALIVE;
        if (!is_kept) { StreamClient::disconnect(); }
    }

    void HttpClient::dropReply()
    {
        _reply.begin();
        _body = HttpBodyReader();
        _persistence = Persistence::CLOSE;
    }

    KiottyNetResult HttpClient::pumpBody(Span<char> buffer, const Deadline& deadline)
    {
        for (;;)
        {
            const TimeLeftResult left = timeLeftOf(deadline);
            if (!left) { return resultOf(0, left.code); }
            const StagedResult staged = peekReceived(left.value);
            if (!staged) { return endInput(staged.code); }
            const BodyStep step = _body.take(staged.value, buffer);
            consumeReceived(step.consumed);
            if (step.code != KIOTTY_NET_CODE_SUCCESS)
            {
                StreamClient::disconnect();
                return resultOf(0, step.code);
            }
            if (step.is_finished) { finishBody(); }
            const bool is_answered = step.produced > 0 || step.is_finished;
            if (is_answered) { return resultOf(step.produced, step.code); }
        }
    }

    KiottyNetResult HttpClient::endInput(KiottyNetCode code)
    {
        if (code == KIOTTY_NET_CODE_TIMEOUT) { return resultOf(0, code); }
        const bool is_closed_by_peer = code == KIOTTY_NET_CODE_PEER_CLOSED;
        const KiottyNetCode ended = is_closed_by_peer ? _body.closeInput() : code;
        StreamClient::disconnect();
        return resultOf(0, ended);
    }

    Span<char> HttpClient::sharedArea()
    {
        const size_t half = _head_bytes.size() / HEAD_AREA_COUNT;
        return Span<char>{ _head_bytes.data(), half };
    }

    Span<char> HttpClient::requestArea()
    {
        const size_t half = _head_bytes.size() / HEAD_AREA_COUNT;
        return Span<char>{ _head_bytes.data() + half, half };
    }

    namespace
    {
        TimeLeftResult timeLeftOf(const Deadline& deadline)
        {
            const uint32_t left = deadline.remaining();
            const bool is_expired = deadline.limited() && left == 0;
            if (is_expired) { return TimeLeftResult::failure(KIOTTY_NET_CODE_TIMEOUT); }
            return TimeLeftResult::success(left);
        }

        KiottyNetHttpReply failedReply(KiottyNetCode code)
        {
            return KiottyNetHttpReply{ code, 0, nullptr, 0 };
        }

        KiottyNetResult resultOf(size_t bytes, KiottyNetCode code)
        {
            KiottyNetResult result{};
            result.bytes = static_cast<int32_t>(bytes);
            result.code = code;
            return result;
        }

        KiottyNetCode checkLimits(const KiottyNetHttpLimits& limits)
        {
            KiottyNetHttpLimits checked = limits;
            const size_t header_bytes = limits.max_header_bytes;
            return configureHttpHeaderCapacity(checked, header_bytes, limits.max_headers);
        }

        uint16_t defaultPortOf(const KiottyNetTlsClientConfiguration* tls)
        {
            return tls == nullptr ? HTTP_DEFAULT_PORT : HTTPS_DEFAULT_PORT;
        }

        Span<char> cappedOf(Span<char> buffer)
        {
            const bool is_large = buffer.length > RESULT_MAXIMUM_BYTES;
            return Span<char>{ buffer.data, is_large ? RESULT_MAXIMUM_BYTES : buffer.length };
        }
    }
}
