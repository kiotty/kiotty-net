#ifndef KIOTTY_NET_HTTP_H
#define KIOTTY_NET_HTTP_H

#include "kiotty_net_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_NET_MAX_URL_LENGTH          256
#define KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH 64

typedef enum KiottyNetHttpMethod
{
    KIOTTY_NET_HTTP_METHOD_GET,
    KIOTTY_NET_HTTP_METHOD_HEAD,
    KIOTTY_NET_HTTP_METHOD_POST,
    KIOTTY_NET_HTTP_METHOD_PUT,
    KIOTTY_NET_HTTP_METHOD_PATCH,
    KIOTTY_NET_HTTP_METHOD_DELETE,
    KIOTTY_NET_HTTP_METHOD_OPTIONS,
    KIOTTY_NET_HTTP_METHOD_COUNT
} KiottyNetHttpMethod;

#define KIOTTY_NET_HTTP_METHOD_BIT(method) (1u << (method))

typedef struct KiottyNetHttpLimits
{
    uint32_t initialization_mark;
    size_t   max_routes;
    size_t   max_header_bytes;
    size_t   max_headers;
    size_t   max_body_bytes;
    size_t   response_bytes;
    uint32_t header_timeout_milliseconds;
    uint32_t keep_alive_timeout_milliseconds;
    uint32_t websocket_ping_interval_milliseconds;
    uint32_t websocket_pong_timeout_milliseconds;
    uint32_t websocket_close_timeout_milliseconds;
} KiottyNetHttpLimits;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeHttpLimits(KiottyNetHttpLimits* limits);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpMaxRoutes(
    KiottyNetHttpLimits* limits,
    size_t max_routes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpHeaderCapacity(
    KiottyNetHttpLimits* limits,
    size_t max_header_bytes,
    size_t max_headers);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpBodyCapacity(
    KiottyNetHttpLimits* limits,
    size_t max_body_bytes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpResponseCapacity(
    KiottyNetHttpLimits* limits,
    size_t response_bytes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpTimeouts(
    KiottyNetHttpLimits* limits,
    uint32_t header_timeout_milliseconds,
    uint32_t keep_alive_timeout_milliseconds);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureHttpWebSocketTimers(
    KiottyNetHttpLimits* limits,
    uint32_t ping_interval_milliseconds,
    uint32_t pong_timeout_milliseconds,
    uint32_t close_timeout_milliseconds);

typedef enum KiottyNetHttpStatus
{
    KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS     = 101,
    KIOTTY_NET_HTTP_STATUS_OK                      = 200,
    KIOTTY_NET_HTTP_STATUS_CREATED                 = 201,
    KIOTTY_NET_HTTP_STATUS_ACCEPTED                = 202,
    KIOTTY_NET_HTTP_STATUS_NO_CONTENT              = 204,
    KIOTTY_NET_HTTP_STATUS_MOVED_PERMANENTLY       = 301,
    KIOTTY_NET_HTTP_STATUS_FOUND                   = 302,
    KIOTTY_NET_HTTP_STATUS_NOT_MODIFIED            = 304,
    KIOTTY_NET_HTTP_STATUS_BAD_REQUEST             = 400,
    KIOTTY_NET_HTTP_STATUS_UNAUTHORIZED            = 401,
    KIOTTY_NET_HTTP_STATUS_FORBIDDEN               = 403,
    KIOTTY_NET_HTTP_STATUS_NOT_FOUND               = 404,
    KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED      = 405,
    KIOTTY_NET_HTTP_STATUS_NOT_ACCEPTABLE          = 406,
    KIOTTY_NET_HTTP_STATUS_REQUEST_TIMEOUT         = 408,
    KIOTTY_NET_HTTP_STATUS_CONFLICT                = 409,
    KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE       = 413,
    KIOTTY_NET_HTTP_STATUS_URI_TOO_LONG            = 414,
    KIOTTY_NET_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE  = 415,
    KIOTTY_NET_HTTP_STATUS_UPGRADE_REQUIRED        = 426,
    KIOTTY_NET_HTTP_STATUS_TOO_MANY_REQUESTS       = 429,
    KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE = 431,
    KIOTTY_NET_HTTP_STATUS_INTERNAL_SERVER_ERROR   = 500,
    KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED         = 501,
    KIOTTY_NET_HTTP_STATUS_SERVICE_UNAVAILABLE     = 503
} KiottyNetHttpStatus;

typedef enum KiottyNetHttpContentType
{
    KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSS,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_CSV,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_MARKDOWN,
    KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT,
    KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_JSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_PROBLEM_JSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_NDJSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_MANIFEST_JSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_XML,
    KIOTTY_NET_HTTP_CONTENT_TYPE_YAML,
    KIOTTY_NET_HTTP_CONTENT_TYPE_FORM_URLENCODED,
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_JSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_XML,
    KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_JSON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_XML,
    KIOTTY_NET_HTTP_CONTENT_TYPE_PROTOBUF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_MSGPACK,
    KIOTTY_NET_HTTP_CONTENT_TYPE_CBOR,
    KIOTTY_NET_HTTP_CONTENT_TYPE_PNG,
    KIOTTY_NET_HTTP_CONTENT_TYPE_JPEG,
    KIOTTY_NET_HTTP_CONTENT_TYPE_GIF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WEBP,
    KIOTTY_NET_HTTP_CONTENT_TYPE_AVIF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_BMP,
    KIOTTY_NET_HTTP_CONTENT_TYPE_SVG,
    KIOTTY_NET_HTTP_CONTENT_TYPE_ICON,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF2,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TTF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_OTF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_MP3,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WAV,
    KIOTTY_NET_HTTP_CONTENT_TYPE_OGG,
    KIOTTY_NET_HTTP_CONTENT_TYPE_MP4,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WEBM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_PDF,
    KIOTTY_NET_HTTP_CONTENT_TYPE_ZIP,
    KIOTTY_NET_HTTP_CONTENT_TYPE_GZIP,
    KIOTTY_NET_HTTP_CONTENT_TYPE_TAR,
    KIOTTY_NET_HTTP_CONTENT_TYPE_WASM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION,
    KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM,
    KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT
} KiottyNetHttpContentType;

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
    KiottyNetBool              keep_alive;
} KiottyNetHttpRequest;

typedef struct KiottyNetHttpServer* KiottyNetHttpServerHandle;

typedef struct KiottyNetHttpResponseHandle
{
    KiottyNetHttpServerHandle server;
    int64_t                   id;
} KiottyNetHttpResponseHandle;

typedef struct KiottyNetHttpWorkHandle
{
    KiottyNetHttpServerHandle server;
    int64_t                   id;
} KiottyNetHttpWorkHandle;

KIOTTY_NET_API KiottyNetSpan KiottyNet_findHttpHeader(
    const KiottyNetHttpRequest* request,
    const char* name);

KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpStatus(
    KiottyNetHttpResponseHandle response,
    int32_t status);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpHeader(
    KiottyNetHttpResponseHandle response,
    const char* name,
    const char* value);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpCustomContentType(
    KiottyNetHttpResponseHandle response,
    const char* content_type);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpBody(
    KiottyNetHttpResponseHandle response,
    KiottyNetHttpContentType type,
    KiottyNetSpan data);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpBodyRef(
    KiottyNetHttpResponseHandle response,
    KiottyNetHttpContentType type,
    KiottyNetSpan data);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpFile(
    KiottyNetHttpResponseHandle response,
    KiottyNetHttpContentType type,
    const char* path);
KIOTTY_NET_API KiottyNetCode KiottyNet_setHttpBodyStream(
    KiottyNetHttpResponseHandle response,
    KiottyNetHttpContentType type);
KIOTTY_NET_API KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle response);

KIOTTY_NET_API KiottyNetResult KiottyNet_writeHttpWorkChunk(
    KiottyNetHttpWorkHandle work,
    KiottyNetSpan data,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetCode KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle work);
KIOTTY_NET_API KiottyNetCode KiottyNet_finishHttpWorkWithClose(
    KiottyNetHttpWorkHandle work,
    KiottyNetCode reason);
KIOTTY_NET_API KiottyNetBool KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle work);

typedef void (*KiottyNetHttpRequestAction)(
    KiottyNetAppContext app_context,
    const KiottyNetHttpRequest* request,
    KiottyNetHttpResponseHandle response);
typedef void (*KiottyNetHttpWorkAction)(
    KiottyNetAppContext app_context,
    const KiottyNetHttpRequest* request,
    KiottyNetHttpResponseHandle response,
    KiottyNetHttpWorkHandle work);

typedef struct KiottyNetHttpRoute
{
    KiottyNetHttpRequestAction onRequest;
    KiottyNetHttpWorkAction    onWorkStarted;
} KiottyNetHttpRoute;

KIOTTY_NET_API KiottyNetHttpRoute KiottyNet_makeHttpRoute(
    KiottyNetHttpRequestAction onRequest,
    KiottyNetHttpWorkAction onWorkStarted);

typedef struct KiottyNetHttpServerResult
{
    KiottyNetCode             code;
    KiottyNetHttpServerHandle handle;
} KiottyNetHttpServerResult;

typedef void (*KiottyNetHttpDisconnectedAction)(
    KiottyNetAppContext app_context,
    KiottyNetConnectionInfo info,
    KiottyNetCode reason);
typedef void (*KiottyNetHttpErrorAction)(
    KiottyNetAppContext app_context,
    KiottyNetConnectionInfo info,
    KiottyNetCode code,
    const char* message);

KIOTTY_NET_API KiottyNetHttpServerResult KiottyNet_createHttpServer(
    KiottyNetContext context,
    const KiottyNetServerConfiguration* configuration,
    const KiottyNetHttpLimits* limits);
KIOTTY_NET_API void                      KiottyNet_destroyHttpServer(
    KiottyNetContext context,
    KiottyNetHttpServerHandle server);
KIOTTY_NET_API KiottyNetCode             KiottyNet_addHttpRoute(
    KiottyNetHttpServerHandle server,
    KiottyNetHttpMethod method,
    const char* path,
    KiottyNetHttpRoute route);
KIOTTY_NET_API KiottyNetCode             KiottyNet_setHttpNotFound(
    KiottyNetHttpServerHandle server,
    KiottyNetHttpRoute route);
KIOTTY_NET_API KiottyNetCode             KiottyNet_onHttpDisconnected(
    KiottyNetHttpServerHandle server,
    KiottyNetHttpDisconnectedAction on_disconnected);
KIOTTY_NET_API KiottyNetCode             KiottyNet_onHttpError(
    KiottyNetHttpServerHandle server,
    KiottyNetHttpErrorAction on_error);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpDirectory(
    KiottyNetHttpServerHandle server,
    const char* url_prefix,
    const char* directory);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpFile(
    KiottyNetHttpServerHandle server,
    const char* url_path,
    const char* file_path);
KIOTTY_NET_API KiottyNetCode             KiottyNet_serveHttpMemory(
    KiottyNetHttpServerHandle server,
    const char* url_path,
    KiottyNetHttpContentType type,
    KiottyNetSpan data);
KIOTTY_NET_API KiottyNetCode             KiottyNet_runHttpServer(
    KiottyNetHttpServerHandle server,
    const KiottyNetAddress* address,
    const KiottyNetTlsServerConfiguration* tls,
    KiottyNetAppContext app_context);
KIOTTY_NET_API void KiottyNet_closeHttpServer(
    KiottyNetHttpServerHandle server);
KIOTTY_NET_API KiottyNetCode             KiottyNet_awaitHttpServer(
    KiottyNetHttpServerHandle server,
    uint64_t timeout_milliseconds);

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
        inline OwnedHandle<Handle, Destroy>& OwnedHandle<Handle, Destroy>::operator=(
            OwnedHandle&& other)
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
    KiottyNet_HttpLimitsBuilder& timeouts(
        uint32_t header_timeout_milliseconds,
        uint32_t keep_alive_timeout_milliseconds);
    KiottyNet_HttpLimitsBuilder& webSocketTimers(
        uint32_t ping_interval_milliseconds,
        uint32_t pong_timeout_milliseconds,
        uint32_t close_timeout_milliseconds);

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
    KiottyNetCode               customContentType(const char* content_type);
    KiottyNetCode               body(KiottyNetHttpContentType type, KiottyNetSpan data);
    KiottyNetCode               bodyRef(KiottyNetHttpContentType type, KiottyNetSpan data);
    KiottyNetCode               file(
        const char* path,
        KiottyNetHttpContentType type = KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION);
    KiottyNetCode               bodyStream(KiottyNetHttpContentType type);
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

    bool            connected() const;
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
    KiottyNetCode   finishWithResponse();
    KiottyNetCode   finishWithClose(KiottyNetCode reason = KIOTTY_NET_CODE_SUCCESS);

private:
    KiottyNetHttpWorkHandle release();

    KiottyNetHttpWorkHandle _work;
};

class KiottyNet_HttpServer
{
public:
    KiottyNet_HttpServer(
        KiottyNetContext context,
        const KiottyNetServerConfiguration& configuration,
        const KiottyNetHttpLimits& limits);

    explicit operator bool() const;
    KiottyNetCode             code() const;
    KiottyNetHttpServerHandle handle() const;

    KiottyNetCode route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRoute route);
    KiottyNetCode route(
        KiottyNetHttpMethod method,
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode get(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode get(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode head(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode head(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode post(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode post(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode put(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode put(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode patch(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode patch(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode del(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode del(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode options(const char* path, KiottyNetHttpRoute route);
    KiottyNetCode options(
        const char* path,
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode notFound(KiottyNetHttpRoute route);
    KiottyNetCode notFound(
        KiottyNetHttpRequestAction on_request);
    KiottyNetCode onDisconnected(KiottyNetHttpDisconnectedAction on_disconnected);
    KiottyNetCode onError(KiottyNetHttpErrorAction on_error);

    KiottyNetCode serveDirectory(const char* url_prefix, const char* directory);
    KiottyNetCode serveFile(const char* url_path, const char* file_path);
    KiottyNetCode serveMemory(const char* url_path, KiottyNetHttpContentType type, KiottyNetSpan data);

    KiottyNetCode run(const KiottyNetAddress& address, KiottyNetAppContext app_context);
    KiottyNetCode run(
        const KiottyNetAddress& address,
        const KiottyNetTlsServerConfiguration& tls,
        KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_milliseconds);

private:
    KiottyNet_HttpServer(KiottyNetContext context, KiottyNetHttpServerResult result);

    KiottyNetCode _code;
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

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::headerCapacity(
    size_t max_header_bytes,
    size_t max_headers)
{
    record(KiottyNet_configureHttpHeaderCapacity(&_limits, max_header_bytes, max_headers));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::bodyCapacity(size_t max_body_bytes)
{
    record(KiottyNet_configureHttpBodyCapacity(&_limits, max_body_bytes));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::responseCapacity(
    size_t response_bytes)
{
    record(KiottyNet_configureHttpResponseCapacity(&_limits, response_bytes));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::timeouts(
    uint32_t header_timeout_milliseconds,
    uint32_t keep_alive_timeout_milliseconds)
{
    record(KiottyNet_configureHttpTimeouts(
        &_limits,
        header_timeout_milliseconds,
        keep_alive_timeout_milliseconds));
    return *this;
}

inline KiottyNet_HttpLimitsBuilder& KiottyNet_HttpLimitsBuilder::webSocketTimers(
    uint32_t ping_interval_milliseconds,
    uint32_t pong_timeout_milliseconds,
    uint32_t close_timeout_milliseconds)
{
    record(KiottyNet_configureHttpWebSocketTimers(
        &_limits,
        ping_interval_milliseconds,
        pong_timeout_milliseconds,
        close_timeout_milliseconds));
    return *this;
}

inline KiottyNetCode KiottyNet_HttpLimitsBuilder::build() const
{
    return _code;
}

inline void KiottyNet_HttpLimitsBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_NET_CODE_SUCCESS)
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

inline KiottyNetCode KiottyNet_HttpResponse::customContentType(const char* content_type)
{
    return KiottyNet_setHttpCustomContentType(_handle, content_type);
}

inline KiottyNetCode KiottyNet_HttpResponse::body(KiottyNetHttpContentType type, KiottyNetSpan data)
{
    return KiottyNet_setHttpBody(_handle, type, data);
}

inline KiottyNetCode KiottyNet_HttpResponse::bodyRef(
    KiottyNetHttpContentType type,
    KiottyNetSpan data)
{
    return KiottyNet_setHttpBodyRef(_handle, type, data);
}

inline KiottyNetCode KiottyNet_HttpResponse::file(const char* path, KiottyNetHttpContentType type)
{
    return KiottyNet_setHttpFile(_handle, type, path);
}

inline KiottyNetCode KiottyNet_HttpResponse::bodyStream(KiottyNetHttpContentType type)
{
    return KiottyNet_setHttpBodyStream(_handle, type);
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
        if (_work.server != nullptr)
        {
            finishWithClose();
        }
        _work = other.release();
    }
    return *this;
}

inline KiottyNet_HttpWork::~KiottyNet_HttpWork()
{
    if (_work.server != nullptr)
    {
        finishWithClose();
    }
}

inline bool KiottyNet_HttpWork::connected() const
{
    return KiottyNet_isHttpWorkConnected(_work) != 0;
}

inline KiottyNetResult KiottyNet_HttpWork::write(KiottyNetSpan data, uint32_t timeout_milliseconds)
{
    return KiottyNet_writeHttpWorkChunk(_work, data, timeout_milliseconds);
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
    _work = KiottyNetHttpWorkHandle{ nullptr, 0 };
    return work;
}

inline KiottyNet_HttpServer::KiottyNet_HttpServer(
    KiottyNetContext context,
    const KiottyNetServerConfiguration& configuration,
    const KiottyNetHttpLimits& limits)
    : KiottyNet_HttpServer(context, KiottyNet_createHttpServer(context, &configuration, &limits))
{
}

inline KiottyNet_HttpServer::KiottyNet_HttpServer(
    KiottyNetContext context,
    KiottyNetHttpServerResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline KiottyNet_HttpServer::operator bool() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetCode KiottyNet_HttpServer::code() const
{
    return _code;
}

inline KiottyNetHttpServerHandle KiottyNet_HttpServer::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_HttpServer::route(
    KiottyNetHttpMethod method,
    const char* path,
    KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), method, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::route(
    KiottyNetHttpMethod method,
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    const KiottyNetHttpRoute route = KiottyNet_makeHttpRoute(on_request, nullptr);
    return KiottyNet_addHttpRoute(_handle.get(), method, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::get(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_GET, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::get(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_GET, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::head(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_HEAD, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::head(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_HEAD, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::post(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_POST, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::post(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_POST, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::put(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_PUT, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::put(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_PUT, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::patch(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_PATCH, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::patch(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_PATCH, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::del(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_DELETE, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::del(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_DELETE, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::options(const char* path, KiottyNetHttpRoute route)
{
    return KiottyNet_addHttpRoute(_handle.get(), KIOTTY_NET_HTTP_METHOD_OPTIONS, path, route);
}

inline KiottyNetCode KiottyNet_HttpServer::options(
    const char* path,
    KiottyNetHttpRequestAction on_request)
{
    return route(KIOTTY_NET_HTTP_METHOD_OPTIONS, path, on_request);
}

inline KiottyNetCode KiottyNet_HttpServer::notFound(KiottyNetHttpRoute route)
{
    return KiottyNet_setHttpNotFound(_handle.get(), route);
}

inline KiottyNetCode KiottyNet_HttpServer::notFound(
    KiottyNetHttpRequestAction on_request)
{
    const KiottyNetHttpRoute route = KiottyNet_makeHttpRoute(on_request, nullptr);
    return KiottyNet_setHttpNotFound(_handle.get(), route);
}

inline KiottyNetCode KiottyNet_HttpServer::onDisconnected(
    KiottyNetHttpDisconnectedAction on_disconnected)
{
    return KiottyNet_onHttpDisconnected(_handle.get(), on_disconnected);
}

inline KiottyNetCode KiottyNet_HttpServer::onError(KiottyNetHttpErrorAction on_error)
{
    return KiottyNet_onHttpError(_handle.get(), on_error);
}

inline KiottyNetCode KiottyNet_HttpServer::serveDirectory(
    const char* url_prefix,
    const char* directory)
{
    return KiottyNet_serveHttpDirectory(_handle.get(), url_prefix, directory);
}

inline KiottyNetCode KiottyNet_HttpServer::serveFile(const char* url_path, const char* file_path)
{
    return KiottyNet_serveHttpFile(_handle.get(), url_path, file_path);
}

inline KiottyNetCode KiottyNet_HttpServer::serveMemory(
    const char* url_path,
    KiottyNetHttpContentType type,
    KiottyNetSpan data)
{
    return KiottyNet_serveHttpMemory(_handle.get(), url_path, type, data);
}

inline KiottyNetCode KiottyNet_HttpServer::run(
    const KiottyNetAddress& address,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runHttpServer(_handle.get(), &address, nullptr, app_context);
}

inline KiottyNetCode KiottyNet_HttpServer::run(
    const KiottyNetAddress& address,
    const KiottyNetTlsServerConfiguration& tls,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runHttpServer(_handle.get(), &address, &tls, app_context);
}

inline void KiottyNet_HttpServer::close()
{
    KiottyNet_closeHttpServer(_handle.get());
}

inline KiottyNetCode KiottyNet_HttpServer::await(uint64_t timeout_milliseconds)
{
    return KiottyNet_awaitHttpServer(_handle.get(), timeout_milliseconds);
}

#endif

#endif
