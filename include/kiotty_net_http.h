#ifndef KIOTTY_NET_HTTP_H
#define KIOTTY_NET_HTTP_H

#include "kiotty_net_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_MAX_URL_LENGTH          256
#define KIOTTY_MAX_CONTENT_TYPE_LENGTH 64

typedef enum KiottyNetHttpMethod
{
    KIOTTY_HTTP_GET,
    KIOTTY_HTTP_HEAD,
    KIOTTY_HTTP_POST,
    KIOTTY_HTTP_PUT,
    KIOTTY_HTTP_PATCH,
    KIOTTY_HTTP_DELETE,
    KIOTTY_HTTP_OPTIONS,
    KIOTTY_HTTP_METHOD_COUNT
} KiottyNetHttpMethod;

#define KIOTTY_HTTP_METHOD_BIT(method) (1u << (method))

typedef struct KiottyNetHttpLimits
{
    uint32_t initialization_mark;
    size_t   max_routes;
    size_t   max_header_bytes;
    size_t   max_headers;
    size_t   max_body_bytes;
    size_t   response_bytes;
    uint32_t header_timeout_ms;
    uint32_t keep_alive_timeout_ms;
} KiottyNetHttpLimits;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeHttpLimits(KiottyNetHttpLimits* limits);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpMaxRoutes(KiottyNetHttpLimits* limits, size_t max_routes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpHeaderCapacity(KiottyNetHttpLimits* limits, size_t max_header_bytes, size_t max_headers);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpBodyCapacity(KiottyNetHttpLimits* limits, size_t max_body_bytes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpResponseCapacity(KiottyNetHttpLimits* limits, size_t response_bytes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpTimeouts(KiottyNetHttpLimits* limits, uint32_t header_timeout_ms, uint32_t keep_alive_timeout_ms);

typedef struct KiottyNetHttpHeader
{
    KiottyNetSpan name;
    KiottyNetSpan value;
} KiottyNetHttpHeader;

typedef struct KiottyNetHttpRequest
{
    KiottyNetConnectionHandle  connection;
    KiottyNetHttpMethod        method;
    KiottyNetSpan              path;
    KiottyNetSpan              query;
    const KiottyNetHttpHeader* headers;
    size_t                     header_count;
    KiottyNetSpan              body;
    int                        keep_alive;
} KiottyNetHttpRequest;

typedef int64_t KiottyNetHttpResponseHandle;
typedef int64_t KiottyNetHttpWorkHandle;

KIOTTY_NET_API KiottyNetSpan KiottyNet_findHttpHeader(const KiottyNetHttpRequest* request, const char* name);

KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpStatus(KiottyNetHttpResponseHandle response, int32_t status);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpHeader(KiottyNetHttpResponseHandle response, const char* name, const char* value);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpBody(KiottyNetHttpResponseHandle response, KiottyNetSpan data, const char* content_type);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpBodyRef(KiottyNetHttpResponseHandle response, KiottyNetSpan data, const char* content_type);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpFile(KiottyNetHttpResponseHandle response, const char* path, const char* content_type);
KIOTTY_NET_API KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle response);

KIOTTY_NET_API KiottyNetCode KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle work);
KIOTTY_NET_API KiottyNetCode KiottyNet_finishHttpWorkWithClose(KiottyNetHttpWorkHandle work, KiottyNetCode reason);
KIOTTY_NET_API bool          KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle work);

typedef void (*KiottyNetHttpRequestAction)(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response);
typedef void (*KiottyNetHttpWorkAction)(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work);

typedef struct KiottyNetHttpRoute
{
    KiottyNetAppContext        app_context;
    KiottyNetHttpRequestAction onRequest;
    KiottyNetHttpWorkAction    onWorkStarted;
} KiottyNetHttpRoute;

KIOTTY_NET_API KiottyNetHttpRoute KiottyNet_makeHttpRoute(KiottyNetAppContext app_context, KiottyNetHttpRequestAction onRequest, KiottyNetHttpWorkAction onWorkStarted);

typedef struct KiottyNetHttpServer* KiottyNetHttpServerHandle;

KIOTTY_NET_API KiottyNetHttpServerHandle KiottyNet_createHttpServer(KiottyNetContext context, const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls, const KiottyNetHttpLimits* limits);
KIOTTY_NET_API void                      KiottyNet_destroyHttpServer(KiottyNetContext context, KiottyNetHttpServerHandle server);
KIOTTY_NET_API KiottyNetCode             KiottyNet_addHttpRoute(KiottyNetHttpServerHandle server, KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
KIOTTY_NET_API KiottyNetCode             KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpDirectory(KiottyNetHttpServerHandle server, const char* url_prefix, const char* directory);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpFile(KiottyNetHttpServerHandle server, const char* url_path, const char* file_path);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpMemory(KiottyNetHttpServerHandle server, const char* url_path, KiottyNetSpan data, const char* content_type);
KIOTTY_NET_API KiottyNetCode             KiottyNet_runHttpServer(KiottyNetHttpServerHandle server);
KIOTTY_NET_API void                      KiottyNet_closeHttpServer(KiottyNetHttpServerHandle server);
KIOTTY_NET_API KiottyNetCode             KiottyNet_awaitHttpServer(KiottyNetHttpServerHandle server, uint64_t timeout_ms);

#ifdef __cplusplus
}

#ifndef KIOTTY_NET_OWNED_HANDLE_DEFINED
#define KIOTTY_NET_OWNED_HANDLE_DEFINED

namespace kiotty
{
    namespace net
    {
        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        class OwnedHandle
        {
        public:
            OwnedHandle(KiottyNetContext context, Handle handle);
            OwnedHandle(OwnedHandle&& other);
            OwnedHandle& operator=(OwnedHandle&& other);
            ~OwnedHandle();

            OwnedHandle(const OwnedHandle&) = delete;
            OwnedHandle& operator=(const OwnedHandle&) = delete;

            Handle get() const;

        private:
            Handle release();
            void   reset();

            KiottyNetContext _context;
            Handle           _handle;
        };

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::OwnedHandle(KiottyNetContext context, Handle handle)
            : _context(context)
            , _handle(handle)
        {
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::OwnedHandle(OwnedHandle&& other)
            : _context(other._context)
            , _handle(other.release())
        {
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>& OwnedHandle<Handle, Destroy>::operator=(OwnedHandle&& other)
        {
            if (this != &other)
            {
                reset();
                _context = other._context;
                _handle  = other.release();
            }
            return *this;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::~OwnedHandle()
        {
            reset();
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline Handle OwnedHandle<Handle, Destroy>::get() const
        {
            return _handle;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline Handle OwnedHandle<Handle, Destroy>::release()
        {
            Handle handle = _handle;
            _handle = nullptr;
            return handle;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline void OwnedHandle<Handle, Destroy>::reset()
        {
            if (_handle == nullptr)
            {
                return;
            }
            Destroy(_context, _handle);
            _handle = nullptr;
        }
    }
}

#endif

class KiottyNet_HttpLimitsBuilder
{
public:
    explicit KiottyNet_HttpLimitsBuilder(KiottyNetHttpLimits& limits);

    KiottyNet_HttpLimitsBuilder& maxRoutes(size_t max_routes);
    KiottyNet_HttpLimitsBuilder& headerCapacity(size_t max_header_bytes, size_t max_headers);
    KiottyNet_HttpLimitsBuilder& bodyCapacity(size_t max_body_bytes);
    KiottyNet_HttpLimitsBuilder& responseCapacity(size_t response_bytes);
    KiottyNet_HttpLimitsBuilder& timeouts(uint32_t header_timeout_ms, uint32_t keep_alive_timeout_ms);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetHttpLimits& _limits;
    KiottyNetCode        _code;
};

class KiottyNet_HttpResponse
{
public:
    explicit KiottyNet_HttpResponse(KiottyNetHttpResponseHandle handle);

    KiottyNetHttpResponseHandle handle() const;
    KiottyNetCode               status(int32_t status);
    KiottyNetCode               header(const char* name, const char* value);
    KiottyNetCode               body(KiottyNetSpan data, const char* content_type);
    KiottyNetCode               bodyRef(KiottyNetSpan data, const char* content_type);
    KiottyNetCode               file(const char* path, const char* content_type = nullptr);
    KiottyNetCode               startWork();

private:
    KiottyNetHttpResponseHandle _handle;
};

class KiottyNet_HttpWork
{
public:
    explicit KiottyNet_HttpWork(KiottyNetHttpWorkHandle work);
    KiottyNet_HttpWork(KiottyNet_HttpWork&& other);
    KiottyNet_HttpWork& operator=(KiottyNet_HttpWork&& other);
    ~KiottyNet_HttpWork();

    KiottyNet_HttpWork(const KiottyNet_HttpWork&) = delete;
    KiottyNet_HttpWork& operator=(const KiottyNet_HttpWork&) = delete;

    bool          connected() const;
    KiottyNetCode finishWithResponse();
    KiottyNetCode finishWithClose(KiottyNetCode reason = KIOTTY_SUCCESS);

private:
    KiottyNetHttpWorkHandle release();

    KiottyNetHttpWorkHandle _work;
};

class KiottyNet_HttpServer
{
public:
    KiottyNet_HttpServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetHttpLimits& limits);
    KiottyNet_HttpServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const KiottyNetHttpLimits& limits);

    bool                      valid() const;
    KiottyNetHttpServerHandle handle() const;

    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode get(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode get(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode head(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode head(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode post(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode post(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode put(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode put(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode patch(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode patch(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode del(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode del(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode options(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode options(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);
    KiottyNetCode notFound(KiottyNetHttpRoute route);
    KiottyNetCode notFound(KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started = nullptr, KiottyNetAppContext app_context = nullptr);

    KiottyNetCode serveDirectory(const char* url_prefix, const char* directory);
    KiottyNetCode serveFile(const char* url_path, const char* file_path);
    KiottyNetCode serveMemory(const char* url_path, KiottyNetSpan data, const char* content_type);

    KiottyNetCode run();
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);

private:
    kiotty::net::OwnedHandle<KiottyNetHttpServerHandle, KiottyNet_destroyHttpServer> _handle;
};

inline KiottyNet_HttpLimitsBuilder::KiottyNet_HttpLimitsBuilder(KiottyNetHttpLimits& limits)
    : _limits(limits)
    , _code(KiottyNet_initializeHttpLimits(&limits))
{
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::maxRoutes(size_t max_routes)
{
    record(KiottyNet_configureHttpMaxRoutes(&_limits, max_routes));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::headerCapacity(size_t max_header_bytes, size_t max_headers)
{
    record(KiottyNet_configureHttpHeaderCapacity(&_limits, max_header_bytes, max_headers));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::bodyCapacity(size_t max_body_bytes)
{
    record(KiottyNet_configureHttpBodyCapacity(&_limits, max_body_bytes));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::responseCapacity(size_t response_bytes)
{
    record(KiottyNet_configureHttpResponseCapacity(&_limits, response_bytes));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::timeouts(uint32_t header_timeout_ms, uint32_t keep_alive_timeout_ms)
{
    record(KiottyNet_configureHttpTimeouts(&_limits, header_timeout_ms, keep_alive_timeout_ms));
    return *this;
}

inline KiottyNetCode KiottyNet_HttpLimitsBuilder::build() const
{
    return _code;
}

inline void KiottyNet_HttpLimitsBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_HttpResponse::KiottyNet_HttpResponse(KiottyNetHttpResponseHandle handle)
    : _handle(handle)
{
}

inline KiottyNetHttpResponseHandle KiottyNet_HttpResponse::handle() const
{
    return _handle;
}

inline KiottyNetCode KiottyNet_HttpResponse::status(int32_t status)
{
    return KiottyNet_setHttpStatus(_handle, status);
}

inline KiottyNetCode KiottyNet_HttpResponse::header(const char* name, const char* value)
{
    return KiottyNet_setHttpHeader(_handle, name, value);
}

inline KiottyNetCode KiottyNet_HttpResponse::body(KiottyNetSpan data, const char* content_type)
{
    return KiottyNet_setHttpBody(_handle, data, content_type);
}

inline KiottyNetCode KiottyNet_HttpResponse::bodyRef(KiottyNetSpan data, const char* content_type)
{
    return KiottyNet_setHttpBodyRef(_handle, data, content_type);
}

inline KiottyNetCode KiottyNet_HttpResponse::file(const char* path, const char* content_type)
{
    return KiottyNet_setHttpFile(_handle, path, content_type);
}

inline KiottyNetCode KiottyNet_HttpResponse::startWork()
{
    return KiottyNet_startHttpWork(_handle);
}

inline KiottyNet_HttpWork::KiottyNet_HttpWork(KiottyNetHttpWorkHandle work)
    : _work(work)
{
}

inline KiottyNet_HttpWork::KiottyNet_HttpWork(KiottyNet_HttpWork&& other)
    : _work(other.release())
{
}

inline KiottyNet_HttpWork& KiottyNet_HttpWork::operator=(KiottyNet_HttpWork&& other)
{
    if (this != &other)
    {
        if (_work != 0)
        {
            finishWithClose();
        }
        _work = other.release();
    }
    return *this;
}

inline KiottyNet_HttpWork::~KiottyNet_HttpWork()
{
    if (_work != 0)
    {
        finishWithClose();
    }
}

inline bool KiottyNet_HttpWork::connected() const
{
    return KiottyNet_isHttpWorkConnected(_work);
}

inline KiottyNetCode KiottyNet_HttpWork::finishWithResponse()
{
    return KiottyNet_finishHttpWorkWithResponse(release());
}

inline KiottyNetCode KiottyNet_HttpWork::finishWithClose(KiottyNetCode reason)
{
    return KiottyNet_finishHttpWorkWithClose(release(), reason);
}

inline KiottyNetHttpWorkHandle KiottyNet_HttpWork::release()
{
    KiottyNetHttpWorkHandle work = _work;
    _work = 0;
    return work;
}

inline KiottyNet_HttpServer::KiottyNet_HttpServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetHttpLimits& limits)
    : _handle(context, KiottyNet_createHttpServer(context, &config, nullptr, &limits))
{
}

inline KiottyNet_HttpServer::KiottyNet_HttpServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const KiottyNetHttpLimits& limits)
    : _handle(context, KiottyNet_createHttpServer(context, &config, &tls, &limits))
{
}

inline bool KiottyNet_HttpServer::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetHttpServerHandle KiottyNet_HttpServer::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_HttpServer::route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), method, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return KiottyNet_addHttpRoute(_handle.get(), method, path, KiottyNet_makeHttpRoute(app_context, on_request, on_work_started));
}

inline KiottyNetCode KiottyNet_HttpServer::get(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_GET, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::get(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_GET, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::head(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_HEAD, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::head(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_HEAD, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::post(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_POST, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::post(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_POST, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::put(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_PUT, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::put(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_PUT, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::patch(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_PATCH, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::patch(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_PATCH, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::del(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_DELETE, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::del(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_DELETE, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::options(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_HTTP_OPTIONS, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::options(const char* path, KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return route(KIOTTY_HTTP_OPTIONS, path, on_request, on_work_started, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::notFound(KiottyNetHttpRoute route)
{
    return KiottyNet_setHttpNotFound(_handle.get(), route);
}

inline KiottyNetCode KiottyNet_HttpServer::notFound(KiottyNetHttpRequestAction on_request, KiottyNetHttpWorkAction on_work_started, KiottyNetAppContext app_context)
{
    return KiottyNet_setHttpNotFound(_handle.get(), KiottyNet_makeHttpRoute(app_context, on_request, on_work_started));
}

inline KiottyNetCode KiottyNet_HttpServer::serveDirectory(const char* url_prefix, const char* directory)
{
    return KiottyNet_serveHttpDirectory(_handle.get(), url_prefix, directory);
}

inline KiottyNetCode KiottyNet_HttpServer::serveFile(const char* url_path, const char* file_path)
{
    return KiottyNet_serveHttpFile(_handle.get(), url_path, file_path);
}

inline KiottyNetCode KiottyNet_HttpServer::serveMemory(const char* url_path, KiottyNetSpan data, const char* content_type)
{
    return KiottyNet_serveHttpMemory(_handle.get(), url_path, data, content_type);
}

inline KiottyNetCode KiottyNet_HttpServer::run()
{
    return KiottyNet_runHttpServer(_handle.get());
}

inline void KiottyNet_HttpServer::close()
{
    KiottyNet_closeHttpServer(_handle.get());
}

inline KiottyNetCode KiottyNet_HttpServer::await(uint64_t timeout_ms)
{
    return KiottyNet_awaitHttpServer(_handle.get(), timeout_ms);
}

#endif

#endif
