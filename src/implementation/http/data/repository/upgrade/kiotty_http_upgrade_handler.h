#pragma once

#include "core/kiotty_span.h"
#include "core/pool/kiotty_pool_handle.h"
#include "http/data/repository/response/kiotty_http_response_store.h"
#include "kiotty_net_base.h"
#include "kiotty_net_http.h"
#include "kiotty_net_statemachine.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class UpgradeAnswer : uint8_t
    {
        ACCEPT,
        REFUSE,
        DROP
    };

    struct UpgradeSession
    {
        const KiottyNetHttpRequest& request;
        HttpResponseStore&          response;
        Span<char>                  message;
        const KiottyNetHttpLimits&  limits;
        size_t                      receive_capacity;
        KiottyNetAppContext         app_context;
        PoolHandle                  id;
        bool                        has_body;
    };

    class HttpUpgradeHandler
    {
    public:
        HttpUpgradeHandler() = default;
        HttpUpgradeHandler(const HttpUpgradeHandler&) = delete;
        HttpUpgradeHandler& operator=(const HttpUpgradeHandler&) = delete;

        virtual UpgradeAnswer       answer(const UpgradeSession& upgrade,
                                           int32_t upgrade_route) = 0;
        virtual KiottyNetTransition open(const UpgradeSession& upgrade) = 0;
        virtual KiottyNetTransition step(const UpgradeSession& upgrade,
                                         KiottyNetConnectionEvent event) = 0;
        virtual void                end(const UpgradeSession& upgrade, KiottyNetCode reason) = 0;

    protected:
        ~HttpUpgradeHandler() = default;
    };
}
