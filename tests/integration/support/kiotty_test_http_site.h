#if !defined(KIOTTY_TEST_HTTP_SITE_H)
#define KIOTTY_TEST_HTTP_SITE_H

#include "kiotty_net_http.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kiotty_test
{
    struct HttpBody
    {
        KiottyNetCode code;
        std::string   text;
    };

    class HttpSite
    {
    public:
        explicit HttpSite(KiottyNetContext context);
        ~HttpSite();

        HttpSite(const HttpSite&) = delete;
        HttpSite& operator=(const HttpSite&) = delete;

        bool start(const KiottyNetHttpLimits& limits, const KiottyNetTlsServerConfiguration* tls);
        void stop();

        const KiottyNetAddress& address() const;
        uint16_t                port() const;
        size_t                  count(KiottyNetHttpMethod method);
        std::vector<int32_t>    ports();
        size_t                  connections();
        bool                    waitSilentStarted(uint32_t timeout_milliseconds);
        void                    releaseSilent();

        void note(KiottyNetHttpMethod method, int32_t port);
        void addWorker(std::thread worker);
        void holdSilent(KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work);

    private:
        bool route(KiottyNetHttpMethod method,
                   const char* path,
                   KiottyNetHttpRequestAction action,
                   KiottyNetHttpWorkAction work);

        KiottyNetContext          _context;
        KiottyNetHttpServerHandle _server;
        KiottyNetAddress          _address;
        uint16_t                  _port;
        std::thread               _thread;
        std::mutex                _mutex;
        std::condition_variable   _changed;
        std::vector<std::thread>  _workers;
        std::vector<int32_t>      _ports;
        size_t                    _counts[KIOTTY_NET_HTTP_METHOD_COUNT];
        bool                      _is_silent_started;
        bool                      _is_silent_released;
    };

    KiottyNetSpan textSpan(const std::string& text);
    std::string   textOf(KiottyNetSpan span);
    HttpBody      readBody(KiottyNetHttpClientHandle client, uint32_t timeout_milliseconds);
    std::string   replyHeader(const KiottyNetHttpReply& reply, const char* name);
    std::string   certificatePath(const char* name);
}

#endif
