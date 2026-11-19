#include "kiotty_net_websocket.h"

#include "health_check_console.h"
#include "health_check_protocol.h"
#include "health_check_service.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace health_check;

namespace
{
    const char* const kWebSocketPath = "/health";

    struct App;

    struct Peer
    {
        App*                     app;
        int64_t                  id;
        KiottyNetWebSocketHandle socket;
        ServiceState             service;
    };

    struct App
    {
        std::mutex               lock;
        int64_t                  next_id;
        std::map<int64_t, Peer*> peers;
        Clock::time_point        started;
        std::atomic<bool>        running;
    };

    struct Outgoing
    {
        int64_t                  id;
        KiottyNetWebSocketHandle socket;
        std::string              message;
    };

    std::string tag(int64_t id)
    {
        return "[" + std::to_string(id) + "] ";
    }

    ServerStats statsOf(const App& app)
    {
        ServerStats stats;
        stats.uptime_ms   = elapsedMilliseconds(app.started, Clock::now());
        stats.connections = static_cast<int>(app.peers.size());
        return stats;
    }

    void sendBinary(const Outgoing& outgoing)
    {
        KiottyNetSpan data = { outgoing.message.data(), outgoing.message.size() };
        KiottyNetCode code = KiottyNet_sendWebSocket(outgoing.socket, KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY, data);
        if (code != KIOTTY_NET_CODE_SUCCESS && code != KIOTTY_NET_CODE_PEER_CLOSED)
        {
            printLine(tag(outgoing.id) + "send failed, code " + std::to_string(code));
        }
    }

    KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket)
    {
        App&  app  = *static_cast<App*>(app_context);
        Peer* peer = new Peer();
        peer->app    = &app;
        peer->socket = websocket;
        resetService(peer->service);
        {
            std::lock_guard<std::mutex> guard(app.lock);
            app.next_id += 1;
            peer->id = app.next_id;
            app.peers[peer->id] = peer;
        }
        KiottyNetWebSocketInfo info = KiottyNet_getWebSocketInfo(websocket);
        printLine(tag(peer->id) + "websocket open from " + info.connection.hostname + ":" + std::to_string(info.connection.port));
        return peer;
    }

    void onMessage(KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketHandle websocket, KiottyNetWebSocketMessage message)
    {
        Peer& peer = *static_cast<Peer*>(websocket_context);
        if (message.opcode != KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY || message.data.length < kHeaderSize)
        {
            printLine(tag(peer.id) + "not a binary request, closing");
            KiottyNet_closeWebSocket(websocket, KIOTTY_NET_WEB_SOCKET_STATUS_UNSUPPORTED_DATA, "binary requests only");
            return;
        }
        Header header = decodeHeader(message.data.data);
        size_t length = message.data.length - kHeaderSize;
        if (header.kind != KIND_REQUEST || header.length != length)
        {
            printLine(tag(peer.id) + "bad header, closing");
            KiottyNet_closeWebSocket(websocket, KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR, "bad header");
            return;
        }
        std::string body(message.data.data + kHeaderSize, length);
        printLine(tag(peer.id) + "request #" + std::to_string(header.request_id) + " " + commandName(header.command) + (body.empty() ? "" : " " + body));
        Outgoing outgoing;
        outgoing.id     = peer.id;
        outgoing.socket = websocket;
        {
            std::lock_guard<std::mutex> guard(peer.app->lock);
            Status      status  = STATUS_OK;
            ServerStats stats   = statsOf(*peer.app);
            std::string reply   = answerRequest(peer.service, header, body, stats, status);
            Command     command = static_cast<Command>(header.command);
            outgoing.message    = encodeMessage(KIND_RESPONSE, command, status, header.request_id, reply);
        }
        sendBinary(outgoing);
    }

    void onClose(KiottyNetAppContext app_context, KiottyNetWebSocketContext websocket_context, KiottyNetWebSocketClose close)
    {
        App&  app  = *static_cast<App*>(app_context);
        Peer* peer = static_cast<Peer*>(websocket_context);
        {
            std::lock_guard<std::mutex> guard(app.lock);
            app.peers.erase(peer->id);
        }
        printLine(tag(peer->id) + "websocket closed, code " + std::to_string(close.code) + " status " + std::to_string(close.status));
        delete peer;
    }

    void onHttpError(KiottyNetAppContext, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        printLine(std::string("http error ") + info.hostname + ":" + std::to_string(info.port) + " code " + std::to_string(code) + " " + message);
    }

    std::vector<Outgoing> collectDueEvents(App& app)
    {
        std::vector<Outgoing>       due;
        std::lock_guard<std::mutex> guard(app.lock);
        Clock::time_point           now   = Clock::now();
        ServerStats                 stats = statsOf(app);
        for (const auto& entry : app.peers)
        {
            Peer&       peer = *entry.second;
            std::string body;
            if (!takeDueEvent(peer.service, now, stats, body))
            {
                continue;
            }
            Outgoing outgoing;
            outgoing.id      = peer.id;
            outgoing.socket  = peer.socket;
            outgoing.message = encodeMessage(KIND_EVENT, COMMAND_EVENT, STATUS_OK, 0, body);
            due.push_back(outgoing);
        }
        return due;
    }

    void runEventTimer(App& app)
    {
        while (app.running)
        {
            std::vector<Outgoing> due = collectDueEvents(app);
            for (const Outgoing& outgoing : due)
            {
                sendBinary(outgoing);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    uint16_t    port = 9200;
    const char* page = KIOTTY_WEBSOCKET_HEALTH_CHECK_PAGE;
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
    KiottyNet_configureServerMaxClients(&configuration, 16);

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

    KiottyNetCode served = KiottyNet_serveHttpFile(server, "/", page);
    if (served != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("serveHttpFile failed, code " + std::to_string(served));
    }
    KiottyNet_onHttpError(server, onHttpError);

    KiottyNetWebSocketRoute route = KiottyNet_addWebSocket(server, kWebSocketPath);
    route = KiottyNet_onWebSocketOpen(route, onOpen);
    route = KiottyNet_onWebSocketMessage(route, onMessage);
    route = KiottyNet_onWebSocketClose(route, onClose);
    if (route.code != KIOTTY_NET_CODE_SUCCESS)
    {
        printLine("websocket route failed, code " + std::to_string(route.code));
        KiottyNet_destroyHttpServer(context.handle, server);
        KiottyNet_releaseContext(context.handle);
        return 1;
    }

    App app;
    app.next_id = 0;
    app.started = Clock::now();
    app.running = true;

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
        std::thread timer([&] {
            runEventTimer(app);
        });
        std::string base = std::string("http://") + host + ":" + std::to_string(port);
        printLine("open " + base + "/ in a browser. websocket at ws://" + host + ":" + std::to_string(port) + kWebSocketPath);
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
        app.running = false;
        timer.join();
    }

    KiottyNet_closeHttpServer(server);
    io.join();
    KiottyNet_destroyHttpServer(context.handle, server);
    KiottyNet_releaseContext(context.handle);
    return result;
}
