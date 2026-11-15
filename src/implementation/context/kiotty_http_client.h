#pragma once

#include "context/kiotty_http_client_headers.h"
#include "context/kiotty_stream_client.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_span.h"
#include "data/repository/http/kiotty_http_reply_store.h"
#include "data/repository/http/kiotty_http_shared_headers.h"
#include "domain/usecases/http/kiotty_http_body_reader.h"
#include "domain/usecases/http/kiotty_http_request_head.h"
#include "kiotty_net_http.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    struct HttpClientConfiguration
    {
        KiottyNetClientConfiguration client;
        KiottyNetHttpLimits          limits;
    };

    struct ClientRequest
    {
        Span<const KiottyNetHttpHeader> headers;
        Span<const char>                path;
        Span<const char>                body;
        KiottyNetHttpMethod             method;
        KiottyNetHttpContentType        type;
    };

    static_assert(sizeof(ClientRequest) == 3 * sizeof(Span<const char>) + 2 * sizeof(int32_t),
                  "ClientRequest: unexpected padding");

    class HttpClient : private StreamClient
    {
    public:
        using code_type = KiottyNetCode;
        using configuration_type = HttpClientConfiguration;

        static constexpr KiottyNetCode TABLE_FULL = KIOTTY_NET_CODE_TABLE_FULL;

        HttpClient();
        ~HttpClient() override;

        HttpClient(const HttpClient&) = delete;
        HttpClient& operator=(const HttpClient&) = delete;
        HttpClient(HttpClient&&) = delete;
        HttpClient& operator=(HttpClient&&) = delete;

        KiottyNetCode      initialize(PoolHandle id, const HttpClientConfiguration& configuration);
        void               reset() noexcept;
        KiottyNetCode      connect(const KiottyNetAddress& address,
                                   const KiottyNetTlsClientConfiguration* tls);
        void               disconnect();
        KiottyNetHttpReply send(const ClientRequest& request, uint32_t timeout_milliseconds);
        KiottyNetResult    readBody(Span<char> buffer, uint32_t timeout_milliseconds);
        HttpClientHeaders  headers();

    private:
        KiottyNetHttpReply exchange(const ClientRequest& request, uint32_t timeout_milliseconds);
        HeadLengthResult   writeHead(const ClientRequest& request);
        KiottyNetCode      transmit(size_t head_length,
                                    Span<const char> body,
                                    const Deadline& deadline);
        KiottyNetCode      writeAll(Span<const char> data, const Deadline& deadline);
        KiottyNetCode      receiveReply(KiottyNetHttpMethod method, const Deadline& deadline);
        KiottyNetCode      receiveHead(const Deadline& deadline);
        LineResult         nextLine(const Deadline& deadline);
        void               startBody(const ReplyFraming& framing);
        void               finishBody();
        void               dropReply();
        KiottyNetResult    pumpBody(Span<char> buffer, const Deadline& deadline);
        KiottyNetResult    endInput(KiottyNetCode code);
        Span<char>         sharedArea();
        Span<char>         requestArea();

        std::vector<char>                _head_bytes;
        std::vector<char>                _reply_bytes;
        std::vector<KiottyNetHttpHeader> _shared_entries;
        std::vector<KiottyNetHttpHeader> _reply_entries;
        HttpSharedHeaders                _shared;
        HttpReplyStore                   _reply;
        HttpBodyReader                   _body;
        KiottyNetAddress                 _address;
        HostField                        _host;
        size_t                           _max_headers;
        Persistence                      _persistence;
        bool                             _has_target;
        std::atomic<bool>                _is_busy;
    };
}
