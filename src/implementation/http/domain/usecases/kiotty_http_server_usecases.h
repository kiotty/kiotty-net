#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/server/kiotty_http_server.h"
#include "http/data/repository/server/kiotty_http_session_host.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetSpan   findHttpHeader(const KiottyNetHttpRequest& request, Span<const char> name);
    KiottyNetCode   setHttpStatus(HttpSessionHost& host, int64_t id, int32_t status);
    KiottyNetCode   setHttpHeader(HttpSessionHost& host,
                                  int64_t id,
                                  const char* name,
                                  const char* value);
    KiottyNetCode   setHttpCustomContentType(HttpSessionHost& host,
                                             int64_t id,
                                             const char* content_type);
    KiottyNetCode   setHttpBody(HttpSessionHost& host,
                                int64_t id,
                                KiottyNetHttpContentType type,
                                KiottyNetSpan data);
    KiottyNetCode   setHttpBodyRef(HttpSessionHost& host,
                                   int64_t id,
                                   KiottyNetHttpContentType type,
                                   KiottyNetSpan data);
    KiottyNetCode   setHttpFile(HttpSessionHost& host,
                                int64_t id,
                                KiottyNetHttpContentType type,
                                const char* path);
    KiottyNetCode   setHttpBodyStream(HttpSessionHost& host,
                                      int64_t id,
                                      KiottyNetHttpContentType type);
    KiottyNetCode   startHttpWork(HttpSessionHost& host, int64_t id);
    KiottyNetResult writeHttpWorkChunk(HttpSessionHost& host,
                                       int64_t id,
                                       KiottyNetSpan data,
                                       uint32_t timeout_milliseconds);
    KiottyNetCode   finishHttpWorkWithResponse(HttpSessionHost& host, int64_t id);
    KiottyNetCode   finishHttpWorkWithClose(HttpSessionHost& host,
                                            int64_t id,
                                            KiottyNetCode reason);
    bool            isHttpWorkConnected(HttpSessionHost& host, int64_t id);
    KiottyNetCode   addHttpRoute(HttpServer& server,
                                 KiottyNetHttpMethod method,
                                 const char* path,
                                 KiottyNetHttpRoute route);
    KiottyNetCode   setHttpNotFound(HttpServer& server, KiottyNetHttpRoute route);
    KiottyNetCode   onHttpDisconnected(HttpServer& server,
                                       KiottyNetHttpDisconnectedAction on_disconnected);
    KiottyNetCode   onHttpError(HttpServer& server, KiottyNetHttpErrorAction on_error);
    KiottyNetCode   serveHttpDirectory(HttpServer& server,
                                       const char* url_prefix,
                                       const char* directory);
    KiottyNetCode   serveHttpFile(HttpServer& server, const char* url_path, const char* file_path);
    KiottyNetCode   serveHttpMemory(HttpServer& server,
                                    const char* url_path,
                                    KiottyNetHttpContentType type,
                                    KiottyNetSpan data);
}
