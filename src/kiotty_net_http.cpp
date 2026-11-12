#include "kiotty_net_http.h"

#include "context/kiotty_context.h"
#include "context/kiotty_http_configuration.h"
#include "core/kiotty_text.h"
#include "data/repository/http/kiotty_http_session.h"

namespace net = kiotty::net::implementation;

namespace
{
    constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    constexpr size_t        MAX_HEADER_NAME_TEXT = 256;
    constexpr KiottyNetSpan STREAM_PLACEHOLDER = { nullptr, 0 };

    using FillingResult = net::Result<net::HttpSession&, KiottyNetCode>;

    FillingResult fillingOf(KiottyNetHttpResponseHandle response)
    {
        if (response.server == nullptr) { return FillingResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        return response.server->work().fill(response.id);
    }

    KiottyNetCode setBody(KiottyNetHttpResponseHandle response,
                          KiottyNetHttpContentType type,
                          KiottyNetSpan data,
                          net::BodyStorage storage)
    {
        const FillingResult filling = fillingOf(response);
        if (!filling) { return filling.code; }
        return filling.value().response.setBody(type, data, storage);
    }
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
    const KiottyNetSpan none = { nullptr, 0 };
    if (request == nullptr || request->headers == nullptr) { return none; }
    const net::TextResult wanted = net::parseText(name, MAX_HEADER_NAME_TEXT);
    if (!wanted) { return none; }
    for (size_t i = 0; i < request->header_count; ++i)
    {
        const KiottyNetHttpHeader& header = request->headers[i];
        const net::Span<const char> found{ header.name.data, header.name.length };
        if (net::equalsIgnoreCase(found, wanted.value)) { return header.value; }
    }
    return none;
}

KiottyNetCode KiottyNet_setHttpStatus(KiottyNetHttpResponseHandle response, int32_t status)
{
    const FillingResult filling = fillingOf(response);
    if (!filling) { return filling.code; }
    return filling.value().response.setStatus(status);
}

KiottyNetCode KiottyNet_setHttpHeader(KiottyNetHttpResponseHandle response,
                                      const char* name,
                                      const char* value)
{
    const FillingResult filling = fillingOf(response);
    if (!filling) { return filling.code; }
    net::HttpSession& session = filling.value();
    const size_t limit = session.site.limits.response_bytes;
    const net::TextResult parsed_name = net::parseText(name, limit);
    if (!parsed_name) { return parsed_name.code; }
    const net::TextResult parsed_value = net::parseText(value, limit);
    if (!parsed_value) { return parsed_value.code; }
    return session.response.addHeader(parsed_name.value, parsed_value.value);
}

KiottyNetCode KiottyNet_setHttpCustomContentType(KiottyNetHttpResponseHandle response,
                                                 const char* content_type)
{
    const FillingResult filling = fillingOf(response);
    if (!filling) { return filling.code; }
    return filling.value().response.setCustomContentType(content_type);
}

KiottyNetCode KiottyNet_setHttpBody(KiottyNetHttpResponseHandle response,
                                    KiottyNetHttpContentType type,
                                    KiottyNetSpan data)
{
    return setBody(response, type, data, net::BodyStorage::COPY);
}

KiottyNetCode KiottyNet_setHttpBodyRef(KiottyNetHttpResponseHandle response,
                                       KiottyNetHttpContentType type,
                                       KiottyNetSpan data)
{
    return setBody(response, type, data, net::BodyStorage::REFERENCE);
}

KiottyNetCode KiottyNet_setHttpBodyStream(KiottyNetHttpResponseHandle response,
                                          KiottyNetHttpContentType type)
{
    return setBody(response, type, STREAM_PLACEHOLDER, net::BodyStorage::STREAM);
}

KiottyNetCode KiottyNet_startHttpWork(KiottyNetHttpResponseHandle response)
{
    if (response.server == nullptr) { return KIOTTY_NET_CODE_PEER_CLOSED; }
    return response.server->work().start(response.id);
}

KiottyNetResult KiottyNet_writeHttpWorkChunk(KiottyNetHttpWorkHandle work,
                                             KiottyNetSpan data,
                                             uint32_t timeout_milliseconds)
{
    const KiottyNetResult closed = { 0, KIOTTY_NET_CODE_PEER_CLOSED };
    if (work.server == nullptr) { return closed; }
    return work.server->work().write(work.id, data, timeout_milliseconds);
}

KiottyNetCode KiottyNet_finishHttpWorkWithResponse(KiottyNetHttpWorkHandle work)
{
    if (work.server == nullptr) { return KIOTTY_NET_CODE_PEER_CLOSED; }
    return work.server->work().finishWithResponse(work.id);
}

KiottyNetCode KiottyNet_finishHttpWorkWithClose(KiottyNetHttpWorkHandle work, KiottyNetCode reason)
{
    if (work.server == nullptr) { return KIOTTY_NET_CODE_PEER_CLOSED; }
    return work.server->work().finishWithClose(work.id, reason);
}

KiottyNetBool KiottyNet_isHttpWorkConnected(KiottyNetHttpWorkHandle work)
{
    if (work.server == nullptr) { return 0; }
    const bool is_connected = work.server->work().isConnected(work.id);
    return is_connected ? 1 : 0;
}

KiottyNetHttpRoute KiottyNet_makeHttpRoute(KiottyNetAppContext app_context,
                                           KiottyNetHttpRequestAction onRequest,
                                           KiottyNetHttpWorkAction onWorkStarted)
{
    KiottyNetHttpRoute route = {};
    route.app_context = app_context;
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
    const net::HttpServerResult created = context->createHttpServer(both);
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
    const net::RegistryResult registry = server->registry();
    if (!registry) { return registry.code; }
    return registry.value().addRoute(method, path, route);
}

KiottyNetCode KiottyNet_setHttpNotFound(KiottyNetHttpServerHandle server, KiottyNetHttpRoute route)
{
    if (server == nullptr) { return INVALID; }
    const net::RegistryResult registry = server->registry();
    if (!registry) { return registry.code; }
    return registry.value().setNotFound(route);
}

KiottyNetCode KiottyNet_onHttpDisconnected(KiottyNetHttpServerHandle server,
                                           KiottyNetHttpDisconnectedAction on_disconnected,
                                           KiottyNetAppContext app_context)
{
    if (server == nullptr) { return INVALID; }
    const net::NoticesResult notices = server->notices();
    if (!notices) { return notices.code; }
    notices.value().setDisconnected(on_disconnected, app_context);
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_onHttpError(KiottyNetHttpServerHandle server,
                                    KiottyNetHttpErrorAction on_error,
                                    KiottyNetAppContext app_context)
{
    if (server == nullptr) { return INVALID; }
    const net::NoticesResult notices = server->notices();
    if (!notices) { return notices.code; }
    notices.value().setError(on_error, app_context);
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_runHttpServer(KiottyNetHttpServerHandle server,
                                      const KiottyNetAddress* address,
                                      const KiottyNetTlsServerConfiguration* tls)
{
    if (server == nullptr || address == nullptr) { return INVALID; }
    const KiottyNetAddress copied = *address;
    return server->run(copied, tls);
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
