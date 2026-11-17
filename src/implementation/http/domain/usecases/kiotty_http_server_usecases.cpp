#include "http/domain/usecases/kiotty_http_server_usecases.h"

#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/header/kiotty_http_reply_headers_parser.h"
#include "http/data/repository/server/kiotty_http_route_path_rules.h"
#include "http/data/repository/server/kiotty_http_work.h"
#include "http/data/state/kiotty_http_session.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t        MAX_FILE_PATH_TEXT = KIOTTY_NET_MAX_PATH_LENGTH - 1;
        constexpr KiottyNetSpan STREAM_PLACEHOLDER = { nullptr, 0 };
        constexpr KiottyNetSpan NO_REQUEST_HEADER = { nullptr, 0 };

        using ResponseSessionResult = Result<HttpSession&, KiottyNetCode>;

        ResponseSessionResult responseSessionOf(HttpSessionHost& host, int64_t id)
        {
            return HttpWork(host).fill(id);
        }
    }

    KiottyNetSpan findHttpHeader(const KiottyNetHttpRequest& request, Span<const char> name)
    {
        if (request.headers == nullptr) { return NO_REQUEST_HEADER; }
        const Span<const KiottyNetHttpHeader> headers{ request.headers, request.header_count };
        return findReplyHeader(headers, name);
    }

    KiottyNetCode setHttpStatus(HttpSessionHost& host, int64_t id, int32_t status)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        return session.value().response.setStatus(status);
    }

    KiottyNetCode setHttpHeader(HttpSessionHost& host,
                                int64_t id,
                                const char* name,
                                const char* value)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        const size_t limit = host.limits.response_bytes;
        const TextResult parsed_name = parseText(name, limit);
        if (!parsed_name) { return parsed_name.code; }
        const TextResult parsed_value = parseText(value, limit);
        if (!parsed_value) { return parsed_value.code; }
        return session.value().response.addHeader(parsed_name.value, parsed_value.value);
    }

    KiottyNetCode setHttpCustomContentType(HttpSessionHost& host,
                                           int64_t id,
                                           const char* content_type)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        return session.value().response.setCustomContentType(content_type);
    }

    KiottyNetCode setHttpBody(HttpSessionHost& host,
                              int64_t id,
                              KiottyNetHttpContentType type,
                              KiottyNetSpan data)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        return session.value().response.setBody(type, data, BodyStorage::COPY);
    }

    KiottyNetCode setHttpBodyRef(HttpSessionHost& host,
                                 int64_t id,
                                 KiottyNetHttpContentType type,
                                 KiottyNetSpan data)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        return session.value().response.setBody(type, data, BodyStorage::REFERENCE);
    }

    KiottyNetCode setHttpFile(HttpSessionHost& host,
                              int64_t id,
                              KiottyNetHttpContentType type,
                              const char* path)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        const TextResult parsed = parseText(path, MAX_FILE_PATH_TEXT);
        if (!parsed) { return parsed.code; }
        if (hasParentSegment(parsed.value)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return attachFile(session.value(), type, parsed.value);
    }

    KiottyNetCode setHttpBodyStream(HttpSessionHost& host,
                                    int64_t id,
                                    KiottyNetHttpContentType type)
    {
        const ResponseSessionResult session = responseSessionOf(host, id);
        if (!session) { return session.code; }
        return session.value().response.setBody(type, STREAM_PLACEHOLDER, BodyStorage::STREAM);
    }

    KiottyNetCode startHttpWork(HttpSessionHost& host, int64_t id)
    {
        return HttpWork(host).start(id);
    }

    KiottyNetResult writeHttpWorkChunk(HttpSessionHost& host,
                                       int64_t id,
                                       KiottyNetSpan data,
                                       uint32_t timeout_milliseconds)
    {
        return HttpWork(host).write(id, data, timeout_milliseconds);
    }

    KiottyNetCode finishHttpWorkWithResponse(HttpSessionHost& host, int64_t id)
    {
        return HttpWork(host).finishWithResponse(id);
    }

    KiottyNetCode finishHttpWorkWithClose(HttpSessionHost& host, int64_t id, KiottyNetCode reason)
    {
        return HttpWork(host).finishWithClose(id, reason);
    }

    bool isHttpWorkConnected(HttpSessionHost& host, int64_t id)
    {
        return HttpWork(host).isConnected(id);
    }

    KiottyNetCode addHttpRoute(HttpServer& server,
                               KiottyNetHttpMethod method,
                               const char* path,
                               KiottyNetHttpRoute route)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return registry.code; }
        return registry.value().addRoute(method, path, route);
    }

    KiottyNetCode setHttpNotFound(HttpServer& server, KiottyNetHttpRoute route)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return registry.code; }
        return registry.value().setNotFound(route);
    }

    KiottyNetCode onHttpDisconnected(HttpServer& server,
                                     KiottyNetHttpDisconnectedAction on_disconnected)
    {
        const NoticesResult notices = server.notices();
        if (!notices) { return notices.code; }
        notices.value().setDisconnected(on_disconnected);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onHttpError(HttpServer& server, KiottyNetHttpErrorAction on_error)
    {
        const NoticesResult notices = server.notices();
        if (!notices) { return notices.code; }
        notices.value().setError(on_error);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode serveHttpDirectory(HttpServer& server,
                                     const char* url_prefix,
                                     const char* directory)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return registry.code; }
        return registry.value().addDirectory(url_prefix, directory);
    }

    KiottyNetCode serveHttpFile(HttpServer& server, const char* url_path, const char* file_path)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return registry.code; }
        return registry.value().addFile(url_path, file_path);
    }

    KiottyNetCode serveHttpMemory(HttpServer& server,
                                  const char* url_path,
                                  KiottyNetHttpContentType type,
                                  KiottyNetSpan data)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return registry.code; }
        return registry.value().addMemory(url_path, type, data);
    }
}
