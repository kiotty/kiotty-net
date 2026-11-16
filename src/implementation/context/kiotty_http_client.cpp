#include "context/kiotty_http_client.h"

#include "context/kiotty_http_configuration.h"
#include "core/kiotty_busy_guard.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_result.h"
#include "core/kiotty_text.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "domain/usecases/http/kiotty_http_request_check.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    namespace
    {
        using TimeLeftResult = Result<uint32_t, KiottyNetCode>;

        constexpr size_t HEAD_AREA_COUNT = 2;
        constexpr size_t MAX_INTERIM_REPLIES = 16;

        TimeLeftResult     timeLeftOf(const Deadline& deadline);
        KiottyNetCode      checkLimits(const KiottyNetHttpLimits& limits);
        uint16_t           defaultPortOf(const KiottyNetTlsClientConfiguration* tls);
        KiottyNetClientConfiguration lineReadyOf(const HttpClientConfiguration& configuration);
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
        , _upload(idleUpload())
        , _address()
        , _host()
        , _max_headers(0)
        , _persistence(Persistence::CLOSE)
        , _has_target(false)
        , _has_reply_bytes(false)
        , _busy()
    {
    }

    HttpClient::~HttpClient() = default;

    KiottyNetCode HttpClient::initialize(PoolHandle id,
                                         const HttpClientConfiguration& configuration)
    {
        const KiottyNetHttpLimits& limits = configuration.limits;
        const KiottyNetCode checked = checkLimits(limits);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const KiottyNetCode base = StreamClient::initialize(id, lineReadyOf(configuration));
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
        _upload = idleUpload();
        _max_headers = limits.max_headers;
        _has_target = false;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpClient::reset() noexcept
    {
        StreamClient::reset();
        _shared.clear();
        requestArea().fill('\0');
        dropReply();
        _upload = idleUpload();
        _has_target = false;
    }

    KiottyNetCode HttpClient::connect(const KiottyNetAddress& address,
                                      const KiottyNetTlsClientConfiguration* tls)
    {
        const BusyGuard guard(_busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        settleStop();
        dropReply();
        _upload = idleUpload();
        _has_target = false;
        const KiottyNetCode prepared = prepare(address, tls);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        const HostFieldResult host = hostFieldOf(address, defaultPortOf(tls));
        if (!host) { return host.code; }
        _address = address;
        _host = host.value;
        _has_target = true;
        return reconnect(_address, Deadline(0));
    }

    HttpClientExchange HttpClient::exchange()
    {
        return HttpClientExchange(*this);
    }

    HttpClientHeaders HttpClient::headers()
    {
        return HttpClientHeaders(_shared, _busy);
    }

    HttpClientLink HttpClient::link()
    {
        StreamClient& stream = *this;
        return HttpClientLink(stream, stopSignal(), _busy);
    }

    void HttpClient::settleStop()
    {
        if (stopSignal().accept()) { StreamClient::disconnect(); }
    }

    HeadLengthResult HttpClient::writeHead(const ClientRequest& request, uint64_t body_length)
    {
        const RequestHead head{ _shared.list(), request.headers, request.path,
                                _host.span(),   body_length,     _max_headers,
                                request.method, request.type };
        const HeadLengthResult written = writeRequestHead(requestArea(), head);
        if (!written) { requestArea().fill('\0'); }
        return written;
    }

    KiottyNetCode HttpClient::transmit(size_t head_length,
                                       Span<const char> body,
                                       const Deadline& deadline)
    {
        const bool is_open = isConnected();
        const KiottyNetCode connected =
            is_open ? KIOTTY_NET_CODE_SUCCESS : reconnect(_address, deadline);
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
        const LineResult line = readLine(left.value);
        _has_reply_bytes = _has_reply_bytes || line;
        return line;
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

    void HttpClient::dropUnreadReply()
    {
        if (!_body.isFinished()) { StreamClient::disconnect(); }
        dropReply();
        _has_reply_bytes = false;
    }

    KiottyNetHttpReply HttpClient::replyOf(KiottyNetCode code)
    {
        if (code != KIOTTY_NET_CODE_SUCCESS)
        {
            StreamClient::disconnect();
            return KiottyNetHttpReply{ code, 0, nullptr, 0 };
        }
        const Span<const KiottyNetHttpHeader> headers = _reply.headers();
        return KiottyNetHttpReply{ code, _reply.status(), headers.data, headers.length };
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

        KiottyNetCode checkLimits(const KiottyNetHttpLimits& limits)
        {
            KiottyNetHttpLimits checked = limits;
            const size_t header_bytes = limits.max_header_bytes;
            return configureHttpHeaderCapacity(checked, header_bytes, limits.max_headers);
        }

        KiottyNetClientConfiguration lineReadyOf(const HttpClientConfiguration& configuration)
        {
            KiottyNetClientConfiguration client = configuration.client;
            const size_t line_bytes = configuration.limits.max_header_bytes + LINE_END_BYTES;
            const size_t wanted = std::max(client.receive_capacity, line_bytes);
            client.receive_capacity = std::min(wanted, MemoryLimits::RECEIVE_CAPACITY);
            return client;
        }

        uint16_t defaultPortOf(const KiottyNetTlsClientConfiguration* tls)
        {
            return tls == nullptr ? HTTP_DEFAULT_PORT : HTTPS_DEFAULT_PORT;
        }
    }
}
