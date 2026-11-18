#include "http/data/repository/client/kiotty_http_client.h"

#include "base/data/datasource/kiotty_memory_limits.h"
#include "core/kiotty_busy_guard.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_upload_rules.h"
#include "http/data/repository/client/kiotty_http_client_send.h"
#include "http/data/repository/request/kiotty_http_request_head_builder.h"
#include "http/data/repository/server/kiotty_http_configuration.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t HEAD_AREA_COUNT = 2;

        KiottyNetCode                checkLimits(const KiottyNetHttpLimits& limits);
        uint16_t                     defaultPortOf(const KiottyNetTlsClientConfiguration* tls);
        KiottyNetClientConfiguration lineReadyOf(const HttpClientConfiguration& configuration);
        size_t                       filePieceOf(size_t send_capacity);
    }

    HttpClient::HttpClient()
        : StreamClient()
        , _head_bytes()
        , _reply_bytes()
        , _shared_entries()
        , _reply_entries()
        , _state()
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
        const size_t half = limits.max_header_bytes;
        _head_bytes.assign(HEAD_AREA_COUNT * half, '\0');
        _reply_bytes.assign(half, '\0');
        _shared_entries.assign(limits.max_headers, KiottyNetHttpHeader{});
        _reply_entries.assign(limits.max_headers, KiottyNetHttpHeader{});
        const Span<char> shared_area{ _head_bytes.data(), half };
        const Span<char> reply_area{ _reply_bytes.data(), half };
        const Span<KiottyNetHttpHeader> shared{ _shared_entries.data(), _shared_entries.size() };
        const Span<KiottyNetHttpHeader> reply{ _reply_entries.data(), _reply_entries.size() };
        _state.shared = HttpSharedHeaders(shared_area, shared);
        _state.reply = HttpReplyStore(reply_area, reply);
        _state.head_area = Span<char>{ _head_bytes.data() + half, half };
        _state.max_headers = limits.max_headers;
        _state.file_piece = filePieceOf(configuration.client.send_capacity);
        dropReply(_state);
        _state.upload = idleUpload();
        _state.target = HttpClientTarget{};
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpClient::reset() noexcept
    {
        StreamClient::reset();
        _state.shared.clear();
        _state.head_area.fill('\0');
        dropReply(_state);
        _state.upload = idleUpload();
        _state.target = HttpClientTarget{};
    }

    KiottyNetCode HttpClient::connect(const KiottyNetAddress& address,
                                      const KiottyNetTlsClientConfiguration* tls)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return KIOTTY_NET_CODE_BUSY; }
        settleStop(*this);
        dropReply(_state);
        _state.upload = idleUpload();
        _state.target = HttpClientTarget{};
        const HostFieldResult host = hostFieldOf(address, defaultPortOf(tls));
        if (!host) { return host.code; }
        const KiottyNetCode connected = StreamClient::connect(address, tls);
        if (connected == KIOTTY_NET_CODE_INVALID_ARGUMENT) { return connected; }
        _state.target = HttpClientTarget{ address, host.value, true };
        return connected;
    }

    HttpClientExchange HttpClient::exchange()
    {
        StreamClient& line = *this;
        return HttpClientExchange(_state, line);
    }

    HttpClientHeaders HttpClient::headers()
    {
        return HttpClientHeaders(_state);
    }

    HttpClientLink HttpClient::link()
    {
        StreamClient& line = *this;
        return HttpClientLink(_state, line);
    }

    namespace
    {
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

        KiottyNetClientConfiguration lineReadyOf(const HttpClientConfiguration& configuration)
        {
            KiottyNetClientConfiguration client = configuration.client;
            const size_t line_bytes = configuration.limits.max_header_bytes + LINE_END_BYTES;
            const size_t wanted = std::max(client.receive_capacity, line_bytes);
            client.receive_capacity = std::min(wanted, MemoryLimits::RECEIVE_CAPACITY);
            return client;
        }

        size_t filePieceOf(size_t send_capacity)
        {
            const bool is_set = send_capacity > 0;
            return is_set ? send_capacity : MemoryLimits::SEND_CAPACITY;
        }
    }
}
