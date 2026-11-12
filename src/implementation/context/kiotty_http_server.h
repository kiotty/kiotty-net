#pragma once

#include "context/kiotty_http_configuration.h"
#include "context/kiotty_http_work.h"
#include "context/kiotty_server.h"
#include "core/kiotty_result.h"
#include "data/repository/http/kiotty_http_site.h"
#include "kiotty_net_http.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    using RegistryResult = Result<HttpRegistry&, KiottyNetCode>;
    using NoticesResult = Result<HttpNotices&, KiottyNetCode>;

    class HttpServer : public Server
    {
    public:
        using configuration_type = HttpServerConfiguration;

        HttpServer();
        ~HttpServer() override;

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;
        HttpServer(HttpServer&&) = delete;
        HttpServer& operator=(HttpServer&&) = delete;

        KiottyNetCode  initialize(PoolHandle id, const HttpServerConfiguration& configuration);
        void           reset() noexcept;
        KiottyNetCode  run(const KiottyNetAddress& address,
                           const KiottyNetTlsServerConfiguration* tls);
        RegistryResult registry();
        NoticesResult  notices();
        HttpWork       work();
        void           close();

    private:
        KiottyNetCode prepare(const HttpServerConfiguration& configuration);

        HttpSite             _site;
        std::atomic<int32_t> _runs;
    };
}
