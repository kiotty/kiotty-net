#include "kiotty_net_http.h"

#include "context/kiotty_context.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/server/kiotty_http_configuration.h"
#include "http/domain/usecases/kiotty_http_server_usecases.h"

namespace net = kiotty::net::implementation;

namespace
{
    using net::INVALID;
    constexpr KiottyNetCode RESPONSE_CLOSED = KIOTTY_NET_CODE_PEER_CLOSED;
    constexpr KiottyNetSpan NO_HEADER = { nullptr, 0 };
}

KiottyNetCode KiottyNet_initializeHttpLimits(KiottyNetHttpLimits* limits)
{
    if (limits == nullptr) { return INVALID; }
    return net::initializeHttpLimits(*limits);
}

KiottyNetCode KiottyNet_configureHttpMaxRoutes(KiottyNetHttpLimits* limits, size_t max_routes)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpMaxRoutes(*limits, max_routes);
}

KiottyNetCode KiottyNet_configureHttpHeaderCapacity(KiottyNetHttpLimits* limits,
                                                    size_t max_header_bytes,
                                                    size_t max_headers)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpHeaderCapacity(*limits, max_header_bytes, max_headers);
}

KiottyNetCode KiottyNet_configureHttpBodyCapacity(KiottyNetHttpLimits* limits, size_t max_body_bytes)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpBodyCapacity(*limits, max_body_bytes);
}

KiottyNetCode KiottyNet_configureHttpResponseCapacity(KiottyNetHttpLimits* limits,
                                                      size_t response_bytes)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpResponseCapacity(*limits, response_bytes);
}

KiottyNetCode KiottyNet_configureHttpTimeouts(KiottyNetHttpLimits* limits,
                                              uint32_t header_timeout_milliseconds,
                                              uint32_t keep_alive_timeout_milliseconds)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpTimeouts(*limits,
                                      header_timeout_milliseconds,
                                      keep_alive_timeout_milliseconds);
}

KiottyNetCode KiottyNet_configureHttpWebSocketTimers(KiottyNetHttpLimits* limits,
                                                     uint32_t ping_interval_milliseconds,
                                                     uint32_t pong_timeout_milliseconds,
                                                     uint32_t close_timeout_milliseconds)
{
    if (limits == nullptr) { return INVALID; }
    return net::configureHttpWebSocketTimers(*limits,
                                             ping_interval_milliseconds,
                                             pong_timeout_milliseconds,
                                             close_timeout_milliseconds);
}

KiottyNetSpan KiottyNet_findHttpHeader(const KiottyNetHttpRequest* request, const char* name)
{
    if (request == nullptr) { return NO_HEADER; }
    const net::TextResult wanted = net::parseText(name, net::MAX_HEADER_NAME_TEXT);
    if (!wanted) { return NO_HEADER; }
    return net::findHttpHeader(*request, wanted.value);
}

KiottyNetCode KiottyNet_setHttpStatus(KiottyNetHttpResponseHandle response, int32_t status)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpStatus(response.server->host(), response.id, status);
}

KiottyNetCode KiottyNet_setHttpHeader(KiottyNetHttpResponseHandle response,
                                      const char* name,
                                      const char* value)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpHeader(response.server->host(), response.id, name, value);
}

KiottyNetCode KiottyNet_setHttpCustomContentType(KiottyNetHttpResponseHandle response,
                                                 const char* content_type)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpCustomContentType(response.server->host(), response.id, content_type);
}

KiottyNetCode KiottyNet_setHttpBody(KiottyNetHttpResponseHandle response,
                                    KiottyNetHttpContentType type,
                                    KiottyNetSpan data)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpBody(response.server->host(), response.id, type, data);
}

KiottyNetCode KiottyNet_setHttpBodyRef(KiottyNetHttpResponseHandle response,
                                       KiottyNetHttpContentType type,
                                       KiottyNetSpan data)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpBodyRef(response.server->host(), response.id, type, data);
}

KiottyNetCode KiottyNet_setHttpFile(KiottyNetHttpResponseHandle response,
                                    KiottyNetHttpContentType type,
                                    const char* path)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpFile(response.server->host(), response.id, type, path);
}

KiottyNetCode KiottyNet_setHttpBodyStream(KiottyNetHttpResponseHandle response,
                                          KiottyNetHttpContentType type)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::setHttpBodyStream(response.server->host(), response.id, type);
}

KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle response)
{
    if (response.server == nullptr) { return RESPONSE_CLOSED; }
    return net::startHttpWork(response.server->host(), response.id);
}

KiottyNetResult KiottyNet_writeHttpWorkChunk(KiottyNetHttpWorkHandle work,
                                             KiottyNetSpan data,
                                             uint32_t timeout_milliseconds)
{
    const KiottyNetResult closed = { 0, RESPONSE_CLOSED };
    if (work.server == nullptr) { return closed; }
    return net::writeHttpWorkChunk(work.server->host(), work.id, data, timeout_milliseconds);
}

KiottyNetCode KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle work)
{
    if (work.server == nullptr) { return RESPONSE_CLOSED; }
    return net::finishHttpWorkWithResponse(work.server->host(), work.id);
}

KiottyNetCode KiottyNet_finishHttpWorkWithClose(KiottyNetHttpWorkHandle work, KiottyNetCode reason)
{
    if (work.server == nullptr) { return RESPONSE_CLOSED; }
    return net::finishHttpWorkWithClose(work.server->host(), work.id, reason);
}

KiottyNetBool KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle work)
{
    if (work.server == nullptr) { return 0; }
    const bool is_connected = net::isHttpWorkConnected(work.server->host(), work.id);
    return is_connected ? 1 : 0;
}

KiottyNetHttpRoute KiottyNet_makeHttpRoute(KiottyNetHttpRequestAction onRequest,
                                           KiottyNetHttpWorkAction onWorkStarted)
{
    KiottyNetHttpRoute route = {};
    route.onRequest = onRequest;
    route.onWorkStarted = onWorkStarted;
    return route;
}

KiottyNetHttpServerResult KiottyNet_createHttpServer(KiottyNetContext context,
                                                     const KiottyNetServerConfiguration* configuration,
                                                     const KiottyNetHttpLimits* limits)
{
    KiottyNetHttpServerResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr || limits == nullptr) { return result; }
    const net::HttpServerConfiguration both{ *configuration, *limits };
    const net::HttpServerResult created = context->create(both);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyHttpServer(KiottyNetContext context, KiottyNetHttpServerHandle server)
{
    if (context == nullptr || server == nullptr) { return; }
    context->destroy(*server);
}

KiottyNetCode KiottyNet_addHttpRoute(KiottyNetHttpServerHandle server,
                                     KiottyNetHttpMethod method,
                                     const char* path,
                                     KiottyNetHttpRoute route)
{
    if (server == nullptr) { return INVALID; }
    return net::addHttpRoute(*server, method, path, route);
}

KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route)
{
    if (server == nullptr) { return INVALID; }
    return net::setHttpNotFound(*server, route);
}

KiottyNetCode KiottyNet_onHttpDisconnected(KiottyNetHttpServerHandle server,
                                           KiottyNetHttpDisconnectedAction on_disconnected)
{
    if (server == nullptr) { return INVALID; }
    return net::onHttpDisconnected(*server, on_disconnected);
}

KiottyNetCode KiottyNet_onHttpError(KiottyNetHttpServerHandle server,
                                    KiottyNetHttpErrorAction on_error)
{
    if (server == nullptr) { return INVALID; }
    return net::onHttpError(*server, on_error);
}

KiottyNetCode KiottyNet_serveHttpDirectory(KiottyNetHttpServerHandle server,
                                           const char* url_prefix,
                                           const char* directory)
{
    if (server == nullptr) { return INVALID; }
    return net::serveHttpDirectory(*server, url_prefix, directory);
}

KiottyNetCode KiottyNet_serveHttpFile(KiottyNetHttpServerHandle server,
                                      const char* url_path,
                                      const char* file_path)
{
    if (server == nullptr) { return INVALID; }
    return net::serveHttpFile(*server, url_path, file_path);
}

KiottyNetCode KiottyNet_serveHttpMemory(KiottyNetHttpServerHandle server,
                                        const char* url_path,
                                        KiottyNetHttpContentType type,
                                        KiottyNetSpan data)
{
    if (server == nullptr) { return INVALID; }
    return net::serveHttpMemory(*server, url_path, type, data);
}

KiottyNetCode KiottyNet_runHttpServer(KiottyNetHttpServerHandle server,
                                      const KiottyNetAddress* address,
                                      const KiottyNetTlsServerConfiguration* tls,
                                      KiottyNetAppContext app_context)
{
    if (server == nullptr || address == nullptr) { return INVALID; }
    const KiottyNetAddress copied = *address;
    return server->run(server, copied, tls, app_context);
}

void KiottyNet_closeHttpServer(KiottyNetHttpServerHandle server)
{
    if (server == nullptr) { return; }
    server->close();
}

KiottyNetCode KiottyNet_awaitHttpServer(KiottyNetHttpServerHandle server,
                                        uint64_t timeout_milliseconds)
{
    if (server == nullptr) { return INVALID; }
    return server->await(timeout_milliseconds);
}
