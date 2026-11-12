#pragma once

#include "core/kiotty_result.h"
#include "data/repository/http/kiotty_http_session.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kiotty::net::implementation
{
    using SessionResult = Result<HttpSession&, KiottyNetCode>;

    class HttpSessionPool
    {
    public:
        HttpSessionPool();

        HttpSessionPool(const HttpSessionPool&) = delete;
        HttpSessionPool& operator=(const HttpSessionPool&) = delete;

        KiottyNetCode initialize(HttpSite& site, size_t count, const KiottyNetHttpLimits& limits);
        SessionResult acquire(const KiottyNetConnectionInfo& info);
        void          release(HttpSession& session);
        bool          handBack(HttpSession& session);
        SessionResult find(int64_t id);

    private:
        void keepGenerations();

        std::vector<char>                _block;
        std::vector<KiottyNetHttpHeader> _headers;
        std::vector<InputFile>           _files;
        std::vector<HttpSession>         _sessions;
        std::vector<int32_t>             _free;
        std::vector<int32_t>             _generations;
        std::mutex                       _mutex;
    };

    int32_t nextSessionGeneration(int32_t generation);
}
