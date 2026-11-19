#include "health_check_server_app.h"

#include "health_check_console.h"
#include "health_check_protocol.h"
#include "health_check_service.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace health_check
{
    namespace
    {
        enum ReadPhase
        {
            READ_HEADER,
            READ_BODY
        };

        struct ServerState
        {
            std::atomic<int64_t> next_id;
            std::atomic<int>     connections;
            Clock::time_point    started;
        };

        struct Session
        {
            ServerState* server;
            int64_t      id;
            ReadPhase    phase;
            Header       header;
            ServiceState service;
            std::string  outgoing;
        };

        std::string tag(const Session& session)
        {
            return "[" + std::to_string(session.id) + "] ";
        }

        ServerStats statsOf(const ServerState& server)
        {
            ServerStats stats;
            stats.uptime_ms   = elapsedMilliseconds(server.started, Clock::now());
            stats.connections = server.connections.load();
            return stats;
        }

        KiottyNetConnectionAction nextRead(const Session& session)
        {
            size_t exact = kHeaderSize;
            if (session.phase == READ_BODY)
            {
                exact = session.header.length;
            }
            uint32_t wait = millisecondsUntilEvent(session.service, Clock::now());
            return KiottyNet_thenRead(exact, wait);
        }

        void sendDueEvent(Session& session, KiottyNetConnectionHandle connection)
        {
            std::string body;
            ServerStats stats = statsOf(*session.server);
            if (!takeDueEvent(session.service, Clock::now(), stats, body))
            {
                return;
            }
            std::string   message = encodeMessage(KIND_EVENT, COMMAND_EVENT, STATUS_OK, 0, body);
            KiottyNetSpan data    = { message.data(), message.size() };
            KiottyNetCode code    = KiottyNet_notifyConnection(connection, data);
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                printLine(tag(session) + "event " + std::to_string(session.service.event_seq) + " dropped, code " + std::to_string(code));
            }
        }

        KiottyNetConnectionAction respond(Session& session, KiottyNetConnectionHandle connection, const std::string& body)
        {
            const Header& header = session.header;
            printLine(tag(session) + "request #" + std::to_string(header.request_id) + " " + commandName(header.command) + (body.empty() ? "" : " " + body));
            Status      status  = STATUS_OK;
            ServerStats stats   = statsOf(*session.server);
            std::string reply   = answerRequest(session.service, header, body, stats, status);
            Command     command = static_cast<Command>(header.command);
            session.outgoing    = encodeMessage(KIND_RESPONSE, command, status, header.request_id, reply);
            KiottyNetSpan data  = { session.outgoing.data(), session.outgoing.size() };
            sendDueEvent(session, connection);
            uint32_t wait = millisecondsUntilEvent(session.service, Clock::now());
            return KiottyNet_thenWrite(data, wait);
        }

        KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
        {
            ServerState& server     = *static_cast<ServerState*>(app_context);
            Session*     session    = new Session();
            session->server         = &server;
            session->id             = server.next_id.fetch_add(1) + 1;
            session->phase          = READ_HEADER;
            resetService(session->service);
            server.connections.fetch_add(1);
            printLine(tag(*session) + "connected from " + info.hostname + ":" + std::to_string(info.port));
            return KiottyNet_acceptConnection(session);
        }

        KiottyNetConnectionAction onReady(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle)
        {
            Session& session = *static_cast<Session*>(connection_context);
            return nextRead(session);
        }

        KiottyNetConnectionAction onRead(
            KiottyNetConnectionContext connection_context,
            KiottyNetConnectionHandle connection,
            KiottyNetSpan data)
        {
            Session& session = *static_cast<Session*>(connection_context);
            if (session.phase == READ_BODY)
            {
                std::string body(data.data, data.length);
                session.phase = READ_HEADER;
                return respond(session, connection, body);
            }
            session.header = decodeHeader(data.data);
            if (session.header.kind != KIND_REQUEST || session.header.length > kMaxBody)
            {
                printLine(tag(session) + "bad header, closing");
                return KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
            }
            if (session.header.length == 0)
            {
                return respond(session, connection, std::string());
            }
            session.phase = READ_BODY;
            sendDueEvent(session, connection);
            return nextRead(session);
        }

        KiottyNetConnectionAction onWritten(
            KiottyNetConnectionContext connection_context,
            KiottyNetConnectionHandle connection,
            size_t)
        {
            Session& session = *static_cast<Session*>(connection_context);
            sendDueEvent(session, connection);
            return nextRead(session);
        }

        KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection)
        {
            Session& session = *static_cast<Session*>(connection_context);
            sendDueEvent(session, connection);
            return nextRead(session);
        }

        void onWorkStarted(KiottyNetConnectionContext, KiottyNetConnectionStream stream)
        {
            KiottyNetConnectionAction then = KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS);
            KiottyNet_closeConnectionStream(stream, then);
        }

        void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetCode reason)
        {
            ServerState& server  = *static_cast<ServerState*>(app_context);
            Session*     session = static_cast<Session*>(connection_context);
            server.connections.fetch_sub(1);
            printLine(tag(*session) + "disconnected, code " + std::to_string(reason));
            delete session;
        }

        void onError(KiottyNetAppContext, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
        {
            printLine(std::string("error ") + info.hostname + ":" + std::to_string(info.port) + " code " + std::to_string(code) + " " + message);
        }

        const KiottyNetConnectionHandler handler = {
            onConnected,
            onReady,
            onRead,
            onWritten,
            onTimeout,
            onWorkStarted,
            onDisconnected,
            onError,
        };
    }

    int runServerApp(const KiottyNetAddress& address, const std::string& where)
    {
        KiottyNetContextResult context = KiottyNet_initializeContext();
        if (context.code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("initializeContext failed, code " + std::to_string(context.code));
            return 1;
        }

        KiottyNetServerConfiguration configuration;
        KiottyNet_initializeServerConfiguration(&configuration);
        KiottyNet_configureServerMaxClients(&configuration, 16);

        KiottyNetServerResult created = KiottyNet_createServer(context.handle, &configuration);
        if (created.code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("createServer failed, code " + std::to_string(created.code));
            KiottyNet_releaseContext(context.handle);
            return 1;
        }
        KiottyNetServerHandle server = created.handle;

        ServerState state;
        state.next_id     = 0;
        state.connections = 0;
        state.started     = Clock::now();

        std::thread io([&] {
            KiottyNetCode code = KiottyNet_runServer(server, &address, nullptr, &handler, &state);
            printLine("server loop ended, code " + std::to_string(code));
        });

        KiottyNetCode started = KiottyNet_awaitServer(server, 3000);
        int           result  = 0;
        if (started != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("server did not start, code " + std::to_string(started));
            result = 1;
        }
        else
        {
            printLine("health check server on " + where + ". /quit to exit");
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

        KiottyNet_closeServer(server);
        io.join();
        KiottyNet_destroyServer(context.handle, server);
        KiottyNet_releaseContext(context.handle);
        return result;
    }
}
