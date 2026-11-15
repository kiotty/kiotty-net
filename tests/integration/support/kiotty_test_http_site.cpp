#include "support/kiotty_test_http_site.h"

#include "support/kiotty_test_peer.h"

#include <chrono>
#include <cstring>
#include <set>
#include <utility>

namespace kiotty_test
{
    namespace
    {
        const char*    LOOPBACK_HOST = "127.0.0.1";
        const uint32_t WAIT_MS = 5000;
        const size_t   READ_CHUNK = 64;
        const size_t   MAX_READS = 100000;

        HttpSite& siteOf(KiottyNetAppContext context)
        {
            return *static_cast<HttpSite*>(context);
        }

        std::string methodName(KiottyNetHttpMethod method)
        {
            switch (method)
            {
            case KIOTTY_NET_HTTP_METHOD_GET: return "GET";
            case KIOTTY_NET_HTTP_METHOD_HEAD: return "HEAD";
            case KIOTTY_NET_HTTP_METHOD_POST: return "POST";
            case KIOTTY_NET_HTTP_METHOD_PUT: return "PUT";
            case KIOTTY_NET_HTTP_METHOD_PATCH: return "PATCH";
            case KIOTTY_NET_HTTP_METHOD_DELETE: return "DELETE";
            case KIOTTY_NET_HTTP_METHOD_OPTIONS: return "OPTIONS";
            case KIOTTY_NET_HTTP_METHOD_COUNT: break;
            }
            return "?";
        }

        void onEcho(KiottyNetAppContext context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
        {
            siteOf(context).note(request->method, KiottyNet_getConnectionInfo(request->connection).port);
            const std::string method = methodName(request->method);
            const std::string body = method + ":" + textOf(request->body);
            KiottyNet_setHttpHeader(response, "X-Method", method.c_str());
            KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan(body));
        }

        void onHeaders(KiottyNetAppContext, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
        {
            const std::string body = "auth=" + textOf(KiottyNet_findHttpHeader(request, "Authorization"))
                                     + "|host=" + textOf(KiottyNet_findHttpHeader(request, "Host"));
            KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan(body));
        }

        void onInspect(KiottyNetAppContext, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
        {
            const std::string body = "auth=" + textOf(KiottyNet_findHttpHeader(request, "Authorization"))
                                     + "|agent=" + textOf(KiottyNet_findHttpHeader(request, "User-Agent"))
                                     + "|accept=" + textOf(KiottyNet_findHttpHeader(request, "Accept"))
                                     + "|id=" + textOf(KiottyNet_findHttpHeader(request, "X-Request-Id"));
            KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan(body));
        }

        void onStartWork(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
        {
            KiottyNet_startHttpWork(response);
        }

        void streamEvents(KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
        {
            KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
            const char* events[] = { "data: one\n\n", "data: two\n\n", "data: three\n\n" };
            for (const char* event : events) { KiottyNet_writeHttpWorkChunk(work, textSpan(event), WAIT_MS); }
            KiottyNet_finishHttpWorkWithResponse(work);
        }

        void onStreamStarted(KiottyNetAppContext context,
                             const KiottyNetHttpRequest*,
                             KiottyNetHttpResponseHandle response,
                             KiottyNetHttpWorkHandle work)
        {
            siteOf(context).addWorker(std::thread([response, work]() { streamEvents(response, work); }));
        }

        void onSilentStarted(KiottyNetAppContext context,
                             const KiottyNetHttpRequest*,
                             KiottyNetHttpResponseHandle response,
                             KiottyNetHttpWorkHandle work)
        {
            HttpSite& site = siteOf(context);
            site.addWorker(std::thread([&site, response, work]() { site.holdSilent(response, work); }));
        }
    }

    HttpSite::HttpSite(KiottyNetContext context)
        : _context(context)
        , _server(nullptr)
        , _address()
        , _port(0)
        , _thread()
        , _mutex()
        , _changed()
        , _workers()
        , _ports()
        , _counts()
        , _is_silent_started(false)
        , _is_silent_released(false)
    {
    }

    HttpSite::~HttpSite()
    {
        stop();
    }

    bool HttpSite::start(const KiottyNetHttpLimits& limits, const KiottyNetTlsServerConfiguration* tls)
    {
        KiottyNetServerConfiguration configuration;
        if (KiottyNet_initializeServerConfiguration(&configuration) != KIOTTY_NET_CODE_SUCCESS) { return false; }
        if (KiottyNet_configureServerMaxClients(&configuration, 8) != KIOTTY_NET_CODE_SUCCESS) { return false; }
        const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(_context, &configuration, &limits);
        if (created.code != KIOTTY_NET_CODE_SUCCESS) { return false; }
        _server = created.handle;
        const KiottyNetHttpMethod methods[] = {
            KIOTTY_NET_HTTP_METHOD_GET,   KIOTTY_NET_HTTP_METHOD_POST,   KIOTTY_NET_HTTP_METHOD_PUT,
            KIOTTY_NET_HTTP_METHOD_PATCH, KIOTTY_NET_HTTP_METHOD_DELETE, KIOTTY_NET_HTTP_METHOD_OPTIONS,
        };
        bool is_routed = true;
        for (KiottyNetHttpMethod method : methods) { is_routed = is_routed && route(method, "/echo", onEcho, nullptr); }
        is_routed = is_routed && route(KIOTTY_NET_HTTP_METHOD_GET, "/headers", onHeaders, nullptr);
        is_routed = is_routed && route(KIOTTY_NET_HTTP_METHOD_GET, "/inspect", onInspect, nullptr);
        is_routed = is_routed && route(KIOTTY_NET_HTTP_METHOD_GET, "/stream", onStartWork, onStreamStarted);
        is_routed = is_routed && route(KIOTTY_NET_HTTP_METHOD_GET, "/silent", onStartWork, onSilentStarted);
        if (!is_routed) { return false; }
        _port = unusedPort();
        if (KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, _port) != KIOTTY_NET_CODE_SUCCESS) { return false; }
        _thread = std::thread([this, tls]() { (void)KiottyNet_runHttpServer(_server, &_address, tls, this); });
        return KiottyNet_awaitHttpServer(_server, WAIT_MS) == KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpSite::stop()
    {
        releaseSilent();
        if (_server != nullptr) { KiottyNet_closeHttpServer(_server); }
        if (_thread.joinable()) { _thread.join(); }
        std::vector<std::thread> taken;
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            taken.swap(_workers);
        }
        for (std::thread& worker : taken) { worker.join(); }
        if (_server != nullptr) { KiottyNet_destroyHttpServer(_context, _server); }
        _server = nullptr;
    }

    const KiottyNetAddress& HttpSite::address() const
    {
        return _address;
    }

    uint16_t HttpSite::port() const
    {
        return _port;
    }

    size_t HttpSite::count(KiottyNetHttpMethod method)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        return _counts[method];
    }

    std::vector<int32_t> HttpSite::ports()
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        return _ports;
    }

    size_t HttpSite::connections()
    {
        const std::vector<int32_t> seen = ports();
        return std::set<int32_t>(seen.begin(), seen.end()).size();
    }

    bool HttpSite::waitSilentStarted(uint32_t timeout_milliseconds)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        const std::chrono::milliseconds limit(timeout_milliseconds);
        return _changed.wait_for(lock, limit, [this]() { return _is_silent_started; });
    }

    void HttpSite::releaseSilent()
    {
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            _is_silent_released = true;
        }
        _changed.notify_all();
    }

    void HttpSite::note(KiottyNetHttpMethod method, int32_t port)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _counts[method] += 1;
        _ports.push_back(port);
    }

    void HttpSite::addWorker(std::thread worker)
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _workers.push_back(std::move(worker));
    }

    void HttpSite::holdSilent(KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        KiottyNet_writeHttpWorkChunk(work, textSpan(": open\n\n"), WAIT_MS);
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _is_silent_started = true;
            _changed.notify_all();
            _changed.wait(lock, [this]() { return _is_silent_released; });
        }
        KiottyNet_finishHttpWorkWithResponse(work);
    }

    bool HttpSite::route(KiottyNetHttpMethod method,
                         const char* path,
                         KiottyNetHttpRequestAction action,
                         KiottyNetHttpWorkAction work)
    {
        const KiottyNetHttpRoute made = KiottyNet_makeHttpRoute(action, work);
        return KiottyNet_addHttpRoute(_server, method, path, made) == KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetSpan textSpan(const std::string& text)
    {
        const KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    std::string textOf(KiottyNetSpan span)
    {
        return span.data == nullptr ? std::string() : std::string(span.data, span.length);
    }

    HttpBody readBody(KiottyNetHttpClientHandle client, uint32_t timeout_milliseconds)
    {
        HttpBody body = { KIOTTY_NET_CODE_SUCCESS, std::string() };
        char buffer[READ_CHUNK];
        for (size_t reads = 0; reads < MAX_READS; ++reads)
        {
            const KiottyNetResult got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), timeout_milliseconds);
            body.code = got.code;
            if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0) { break; }
            body.text.append(buffer, static_cast<size_t>(got.bytes));
        }
        return body;
    }

    std::string replyHeader(const KiottyNetHttpReply& reply, const char* name)
    {
        return textOf(KiottyNet_findHttpReplyHeader(&reply, name));
    }

    std::string certificatePath(const char* name)
    {
        return std::string(KIOTTY_TEST_CERT_DIR) + "/" + name;
    }
}
