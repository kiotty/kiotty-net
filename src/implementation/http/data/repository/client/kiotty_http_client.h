#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/pool/kiotty_pool_handle.h"
#include "http/data/repository/client/kiotty_http_client_exchange.h"
#include "http/data/repository/client/kiotty_http_client_headers.h"
#include "http/data/repository/client/kiotty_http_client_link.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "kiotty_net_http.h"

#include <vector>

namespace kiotty::net::implementation
{
    struct HttpClientConfiguration
    {
        KiottyNetClientConfiguration client;
        KiottyNetHttpLimits          limits;
    };

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
        HttpClientExchange exchange();
        HttpClientHeaders  headers();
        HttpClientLink     link();

    private:
        std::vector<char>                _head_bytes;
        std::vector<char>                _reply_bytes;
        std::vector<KiottyNetHttpHeader> _shared_entries;
        std::vector<KiottyNetHttpHeader> _reply_entries;
        HttpClientState                  _state;
    };
}
