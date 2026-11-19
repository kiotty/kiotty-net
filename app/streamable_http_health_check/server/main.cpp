#include "kiotty_net_http.h"

#include "health_check_console.h"
#include "health_check_protocol.h"
#include "health_check_service.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace health_check;

namespace
{
    const char* const kEndpoint             = "/mcp";
    const char* const kSessionHeader        = "Mcp-Session-Id";
    const uint32_t    kChunkTimeoutMs       = 5000;
    const uint32_t    kConnectionCheckMs    = 500;
    const uint32_t    kStreamTickMs         = 10;

    struct Session
    {
        std::string  id;
        int64_t      number;
        ServiceState service;
        bool         streaming;
    };

    struct App
    {
        std::mutex                      lock;
        std::map<std::string, Session*> sessions;
        int64_t                         next_number;
        Clock::time_point               started;
        std::atomic<bool>               running;
        std::vector<std::thread>        streams;
        std::mt19937_64                 random;
    };

    std::string tag(int64_t number)
    {
        return "[s" + std::to_string(number) + "] ";
    }

    ServerStats statsOf(const App& app)
    {
        ServerStats stats;
        stats.uptime_ms   = elapsedMilliseconds(app.started, Clock::now());
        stats.connections = static_cast<int>(app.sessions.size());
        return stats;
    }

    std::string makeSessionId(App& app)
    {
        const char* digits = "0123456789abcdef";
        std::string id;
        for (int part = 0; part < 2; ++part)
        {
            uint64_t value = app.random();
            for (int nibble = 0; nibble < 16; ++nibble)
            {
                id.push_back(digits[value & 0xF]);
                value >>= 4;
            }
        }
        return id;
    }

    std::string encodeBase64(const std::string& input)
    {
        const char* table  = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string output;
        size_t      index  = 0;
        while (index + 2 < input.size())
        {
            uint32_t b0 = static_cast<unsigned char>(input[index]);
            uint32_t b1 = static_cast<unsigned char>(input[index + 1]);
            uint32_t b2 = static_cast<unsigned char>(input[index + 2]);
            uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
            output.push_back(table[(triple >> 18) & 0x3F]);
            output.push_back(table[(triple >> 12) & 0x3F]);
            output.push_back(table[(triple >> 6) & 0x3F]);
            output.push_back(table[triple & 0x3F]);
            index += 3;
        }
        size_t left = input.size() - index;
        if (left == 1)
        {
            uint32_t b0 = static_cast<unsigned char>(input[index]);
            output.push_back(table[(b0 >> 2) & 0x3F]);
            output.push_back(table[(b0 << 4) & 0x3F]);
            output += "==";
        }
        if (left == 2)
        {
            uint32_t b0 = static_cast<unsigned char>(input[index]);
            uint32_t b1 = static_cast<unsigned char>(input[index + 1]);
            output.push_back(table[(b0 >> 2) & 0x3F]);
            output.push_back(table[((b0 << 4) | (b1 >> 4)) & 0x3F]);
            output.push_back(table[(b1 << 2) & 0x3F]);
            output.push_back('=');
        }
        return output;
    }

    std::string headerValue(const KiottyNetHttpRequest* request, const char* name)
    {
        KiottyNetSpan value = KiottyNet_findHttpHeader(request, name);
        if (value.data == nullptr)
        {
            return std::string();
        }
        return std::string(value.data, value.length);
    }

    void respondText(KiottyNetHttpResponseHandle response, int32_t status, const std::string& text)
    {
        KiottyNetSpan body = { text.data(), text.size() };
        KiottyNet_setHttpStatus(response, status);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, body);
    }

    Session* createSession(App& app)
    {
        Session* session = new Session();
        session->id        = makeSessionId(app);
        session->streaming = false;
        resetService(session->service);
        app.next_number += 1;
        session->number = app.next_number;
        app.sessions[session->id] = session;
        return session;
    }

    Session* findSession(App& app, const std::string& id)
    {
        auto found = app.sessions.find(id);
        if (found == app.sessions.end())
        {
            return nullptr;
        }
        return found->second;
    }

    void onPost(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        App&        app = *static_cast<App*>(app_context);
        std::string id  = headerValue(request, kSessionHeader);
        if (request->body.length < kHeaderSize)
        {
            respondText(response, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, "request shorter than the header");
            return;
        }
        Header header = decodeHeader(request->body.data);
        size_t length = request->body.length - kHeaderSize;
        if (header.kind != KIND_REQUEST || header.length != length)
        {
            respondText(response, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, "bad header");
            return;
        }
        std::string body(request->body.data + kHeaderSize, length);
        std::string message;
        std::string session_id;
        int64_t     number  = 0;
        bool        created = false;
        {
            std::lock_guard<std::mutex> guard(app.lock);
            Session* session = nullptr;
            if (id.empty())
            {
                session = createSession(app);
                created = true;
            }
            else
            {
                session = findSession(app, id);
            }
            if (session != nullptr)
            {
                Status      status  = STATUS_OK;
                ServerStats stats   = statsOf(app);
                std::string reply   = answerRequest(session->service, header, body, stats, status);
                Command     command = static_cast<Command>(header.command);
                message    = encodeMessage(KIND_RESPONSE, command, status, header.request_id, reply);
                session_id = session->id;
                number     = session->number;
            }
        }
        if (session_id.empty())
        {
            printLine("unknown session " + id);
            respondText(response, KIOTTY_NET_HTTP_STATUS_NOT_FOUND, "unknown session");
            return;
        }
        if (created)
        {
            printLine(tag(number) + "session created " + session_id);
        }
        printLine(tag(number) + "POST request #" + std::to_string(header.request_id) + " " + commandName(header.command) + (body.empty() ? "" : " " + body));
        KiottyNetSpan data = { message.data(), message.size() };
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_OK);
        KiottyNet_setHttpHeader(response, kSessionHeader, session_id.c_str());
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, data);
    }

    void onDelete(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        App&        app    = *static_cast<App*>(app_context);
        std::string id     = headerValue(request, kSessionHeader);
        int64_t     number = 0;
        {
            std::lock_guard<std::mutex> guard(app.lock);
            Session* session = findSession(app, id);
            if (session != nullptr)
            {
                number = session->number;
                app.sessions.erase(id);
                delete session;
            }
        }
        if (number == 0)
        {
            respondText(response, KIOTTY_NET_HTTP_STATUS_NOT_FOUND, "unknown session");
            return;
        }
        printLine(tag(number) + "session deleted");
        respondText(response, KIOTTY_NET_HTTP_STATUS_OK, "session deleted");
    }

    void onGet(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        App&        app      = *static_cast<App*>(app_context);
        std::string id       = headerValue(request, kSessionHeader);
        bool        found    = false;
        bool        conflict = false;
        {
            std::lock_guard<std::mutex> guard(app.lock);
            Session* session = findSession(app, id);
            if (session != nullptr)
            {
                found    = true;
                conflict = session->streaming;
                if (!conflict && request->method != KIOTTY_NET_HTTP_METHOD_HEAD)
                {
                    session->streaming = true;
                }
            }
        }
        if (!found)
        {
            respondText(response, KIOTTY_NET_HTTP_STATUS_NOT_FOUND, "unknown session");
            return;
        }
        if (conflict)
        {
            respondText(response, KIOTTY_NET_HTTP_STATUS_CONFLICT, "stream already open for this session");
            return;
        }
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_OK);
        KiottyNet_setHttpHeader(response, "Cache-Control", "no-cache");
        KiottyNet_setHttpHeader(response, kSessionHeader, id.c_str());
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        KiottyNet_startHttpWork(response);
    }

    void stopStreaming(App& app, const std::string& id)
    {
        std::lock_guard<std::mutex> guard(app.lock);
        Session* session = findSession(app, id);
        if (session != nullptr)
        {
            session->streaming = false;
        }
    }

    bool writeChunk(KiottyNetHttpWorkHandle work, const std::string& text)
    {
        KiottyNetSpan   data    = { text.data(), text.size() };
        KiottyNetResult written = KiottyNet_writeHttpWorkChunk(work, data, kChunkTimeoutMs);
        return written.code == KIOTTY_NET_CODE_SUCCESS;
    }

    void runStream(App& app, std::string id, int64_t number, KiottyNetHttpWorkHandle work)
    {
        printLine(tag(number) + "event stream opened");
        if (!writeChunk(work, ": stream open\n\n"))
        {
            KiottyNet_finishHttpWorkWithClose(work, KIOTTY_NET_CODE_PEER_CLOSED);
            stopStreaming(app, id);
            printLine(tag(number) + "event stream failed to start");
            return;
        }
        Clock::time_point last_check = Clock::now();
        while (app.running)
        {
            bool        alive = false;
            std::string event;
            {
                std::lock_guard<std::mutex> guard(app.lock);
                Session* session = findSession(app, id);
                if (session != nullptr)
                {
                    alive = true;
                    std::string body;
                    ServerStats stats = statsOf(app);
                    if (takeDueEvent(session->service, Clock::now(), stats, body))
                    {
                        std::string message = encodeMessage(KIND_EVENT, COMMAND_EVENT, STATUS_OK, 0, body);
                        event = "id: " + std::to_string(session->service.event_seq) + "\nevent: message\ndata: " + encodeBase64(message) + "\n\n";
                    }
                }
            }
            if (!alive)
            {
                KiottyNet_finishHttpWorkWithResponse(work);
                printLine(tag(number) + "event stream ended, session deleted");
                return;
            }
            if (!event.empty() && !writeChunk(work, event))
            {
                KiottyNet_finishHttpWorkWithClose(work, KIOTTY_NET_CODE_PEER_CLOSED);
                stopStreaming(app, id);
                printLine(tag(number) + "event stream closed by the client");
                return;
            }
            Clock::time_point now = Clock::now();
            if (elapsedMilliseconds(last_check, now) >= kConnectionCheckMs)
            {
                last_check = now;
                if (KiottyNet_isHttpWorkConnected(work) == 0)
                {
                    KiottyNet_finishHttpWorkWithClose(work, KIOTTY_NET_CODE_PEER_CLOSED);
                    stopStreaming(app, id);
                    printLine(tag(number) + "event stream closed by the client");
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kStreamTickMs));
        }
        KiottyNet_finishHttpWorkWithResponse(work);
        printLine(tag(number) + "event stream ended, server stopping");
    }

    void onGetWorkStarted(
        KiottyNetAppContext app_context,
        const KiottyNetHttpRequest* request,
        KiottyNetHttpResponseHandle,
        KiottyNetHttpWorkHandle work)
    {
        App&        app    = *static_cast<App*>(app_context);
        std::string id     = headerValue(request, kSessionHeader);
        int64_t     number = 0;
        std::lock_guard<std::mutex> guard(app.lock);
        Session* session = findSession(app, id);
        if (session != nullptr)
        {
            number = session->number;
        }
        app.streams.emplace_back([&app, id, number, work] {
            runStream(app, id, number, work);
        });
    }

    void onHttpError(KiottyNetAppContext, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        printLine(std::string("http error ") + info.hostname + ":" + std::to_string(info.port) + " code " + std::to_string(code) + " " + message);
    }

    bool registerRoutes(KiottyNetHttpServerHandle server, const char* page)
    {
        KiottyNetHttpRoute post   = KiottyNet_makeHttpRoute(onPost, nullptr);
        KiottyNetHttpRoute get    = KiottyNet_makeHttpRoute(onGet, onGetWorkStarted);
        KiottyNetHttpRoute remove = KiottyNet_makeHttpRoute(onDelete, nullptr);
        KiottyNetCode      codes[5];
        codes[0] = KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_POST, kEndpoint, post);
        codes[1] = KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, kEndpoint, get);
        codes[2] = KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_DELETE, kEndpoint, remove);
        codes[3] = KiottyNet_serveHttpFile(server, "/", page);
        codes[4] = KiottyNet_onHttpError(server, onHttpError);
        for (KiottyNetCode code : codes)
        {
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                printLine("route registration failed, code " + std::to_string(code));
                return false;
            }
        }
        return true;
    }

    void stopStreams(App& app)
    {
        app.running = false;
        std::vector<std::thread> streams;
        {
            std::lock_guard<std::mutex> guard(app.lock);
            streams.swap(app.streams);
        }
        for (std::thread& stream : streams)
        {
            stream.join();
        }
    }

    void deleteSessions(App& app)
    {
        std::lock_guard<std::mutex> guard(app.lock);
        for (auto& entry : app.sessions)
        {
            delete entry.second;
        }
        app.sessions.clear();
    }
}

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    uint16_t    port = 9300;
    const char* page = KIOTTY_STREAMABLE_HTTP_HEALTH_CHECK_PAGE;
    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    if (argc > 2)
    {
        host = argv[2];
    }
    if (argc > 3)
    {
        page = argv[3];
    }

    KiottyNetAddress address;
    if (KiottyNet_initializeTcpAddress(&address, host, port) != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("bad address");
        return 1;
    }

    KiottyNetContextResult context = KiottyNet_initializeContext();
    if (context.code != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("initializeContext failed, code " + std::to_string(context.code));
        return 1;
    }

    KiottyNetServerConfiguration configuration;
    KiottyNet_initializeServerConfiguration(&configuration);
    KiottyNet_configureServerMaxClients(&configuration, 32);

    KiottyNetHttpLimits limits;
    KiottyNet_initializeHttpLimits(&limits);

    KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context.handle, &configuration, &limits);
    if (created.code != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("createHttpServer failed, code " + std::to_string(created.code));
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    KiottyNetHttpServerHandle server = created.handle;

    if (!registerRoutes(server, page))
    {
        KiottyNet_destroyHttpServer(context.handle, server);
        KiottyNet_releaseContext(context.handle);
        return 1;
    }

    App app;
    app.next_number = 0;
    app.started     = Clock::now();
    app.running     = true;
    std::random_device seed;
    app.random.seed((static_cast<uint64_t>(seed()) << 32) | seed());

    std::thread io([&] {
        KiottyNetCode code = KiottyNet_runHttpServer(server, &address, nullptr, &app);
        printLine("server loop ended, code " + std::to_string(code));
    });

    KiottyNetCode started = KiottyNet_awaitHttpServer(server, 3000);
    int           result  = 0;
    if (started != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("server did not start, code " + std::to_string(started));
        result = 1;
    }
    else
    {
        std::string base = std::string("http://") + host + ":" + std::to_string(port);
        printLine("open " + base + "/ in a browser. endpoint " + base + kEndpoint);
        printLine("page: " + std::string(page));
        printLine("/quit to exit");
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line == "/quit")
            {
                break;
            }
        }
    }

    stopStreams(app);
    KiottyNet_closeHttpServer(server);
    io.join();
    stopStreams(app);
    deleteSessions(app);
    KiottyNet_destroyHttpServer(context.handle, server);
    KiottyNet_releaseContext(context.handle);
    return result;
}
