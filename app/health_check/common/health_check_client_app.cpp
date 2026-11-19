#include "health_check_client_app.h"

#include "health_check_console.h"
#include "health_check_protocol.h"

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

        struct ClientState
        {
            std::atomic<uint32_t> next_request_id;
            std::atomic<uint32_t> fetch_left;
            std::atomic<uint32_t> events_received;
            ReadPhase             phase;
            Header                header;
        };

        std::string makeRequest(ClientState& state, Command command, const std::string& body)
        {
            uint32_t request_id = state.next_request_id.fetch_add(1) + 1;
            printLine("-> #" + std::to_string(request_id) + " " + commandName(command) + (body.empty() ? "" : " " + body));
            return encodeMessage(KIND_REQUEST, command, STATUS_OK, request_id, body);
        }

        KiottyNetConnectionAction nextRead(const ClientState& state)
        {
            size_t exact = kHeaderSize;
            if (state.phase == READ_BODY)
            {
                exact = state.header.length;
            }
            return KiottyNet_thenRead(exact, 0);
        }

        void continueFetch(ClientState& state, KiottyNetConnectionHandle connection, const Header& header)
        {
            uint32_t left = state.fetch_left.load();
            if (left == 0)
            {
                return;
            }
            if (header.status != STATUS_OK)
            {
                state.fetch_left = 0;
                printLine("   auto fetch stopped: " + std::string(statusName(header.status)));
                return;
            }
            left -= 1;
            state.fetch_left = left;
            if (left == 0)
            {
                printLine("   auto fetch done (" + std::to_string(kAutoFetchCount) + " requests)");
                return;
            }
            std::string   message = makeRequest(state, COMMAND_FETCH_CURSOR, std::string());
            KiottyNetSpan data    = { message.data(), message.size() };
            KiottyNetCode code    = KiottyNet_notifyConnection(connection, data);
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                state.fetch_left = 0;
                printLine("   auto fetch send failed, code " + std::to_string(code));
            }
        }

        void handleMessage(ClientState& state, KiottyNetConnectionHandle connection, const std::string& body)
        {
            const Header header = state.header;
            if (header.kind == KIND_EVENT)
            {
                state.events_received.fetch_add(1);
                printLine("[event] " + body);
                return;
            }
            if (header.kind != KIND_RESPONSE)
            {
                printLine("[unknown kind " + std::to_string(header.kind) + "] " + body);
                return;
            }
            printLine("<- #" + std::to_string(header.request_id) + " " + commandName(header.command) + " " + statusName(header.status) + ": " + body);
            if (header.command == COMMAND_FETCH_CURSOR)
            {
                continueFetch(state, connection, header);
            }
        }

        KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
        {
            printLine(std::string("connected to ") + info.hostname + ":" + std::to_string(info.port));
            return KiottyNet_acceptConnection(app_context);
        }

        KiottyNetConnectionAction onReady(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle)
        {
            ClientState& state = *static_cast<ClientState*>(connection_context);
            return nextRead(state);
        }

        KiottyNetConnectionAction onRead(
            KiottyNetConnectionContext connection_context,
            KiottyNetConnectionHandle connection,
            KiottyNetSpan data)
        {
            ClientState& state = *static_cast<ClientState*>(connection_context);
            if (state.phase == READ_BODY)
            {
                std::string body(data.data, data.length);
                state.phase = READ_HEADER;
                handleMessage(state, connection, body);
                return nextRead(state);
            }
            state.header = decodeHeader(data.data);
            if (state.header.length > kMaxBody)
            {
                printLine("body too large, closing");
                return KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
            }
            if (state.header.length == 0)
            {
                handleMessage(state, connection, std::string());
                return nextRead(state);
            }
            state.phase = READ_BODY;
            return nextRead(state);
        }

        KiottyNetConnectionAction onWritten(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle, size_t)
        {
            ClientState& state = *static_cast<ClientState*>(connection_context);
            return nextRead(state);
        }

        KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle)
        {
            ClientState& state = *static_cast<ClientState*>(connection_context);
            return nextRead(state);
        }

        void onWorkStarted(KiottyNetConnectionContext, KiottyNetConnectionStream stream)
        {
            KiottyNetConnectionAction then = KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS);
            KiottyNet_closeConnectionStream(stream, then);
        }

        void onDisconnected(KiottyNetAppContext, KiottyNetConnectionContext, KiottyNetCode reason)
        {
            printLine("disconnected, code " + std::to_string(reason));
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

        void printMenu()
        {
            printLine("1 start events | 2 stop events | 3 events received so far");
            printLine("4 open cursor  | 5 fetch x" + std::to_string(kAutoFetchCount) + "    | 6 close cursor | 7 [text] single request");
            printLine("h menu | q quit");
        }

        void send(KiottyNetEventClientHandle client, const std::string& message)
        {
            KiottyNetSpan data = { message.data(), message.size() };
            KiottyNetCode code = KiottyNet_notifyEventClient(client, data);
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                printLine("send failed, code " + std::to_string(code));
            }
        }

        void runCommand(ClientState& state, KiottyNetEventClientHandle client, const std::string& line)
        {
            char        choice = line[0];
            std::string rest;
            if (line.size() > 2)
            {
                rest = line.substr(2);
            }
            switch (choice)
            {
            case '1':
                send(client, makeRequest(state, COMMAND_START_EVENTS, std::string()));
                return;
            case '2':
                send(client, makeRequest(state, COMMAND_STOP_EVENTS, std::string()));
                return;
            case '3':
                printLine("events received so far: " + std::to_string(state.events_received.load()));
                return;
            case '4':
                send(client, makeRequest(state, COMMAND_OPEN_CURSOR, std::string()));
                return;
            case '5':
                if (state.fetch_left.load() != 0)
                {
                    printLine("auto fetch already running");
                    return;
                }
                state.fetch_left = kAutoFetchCount;
                send(client, makeRequest(state, COMMAND_FETCH_CURSOR, std::string()));
                return;
            case '6':
                send(client, makeRequest(state, COMMAND_CLOSE_CURSOR, std::string()));
                return;
            case '7':
                send(client, makeRequest(state, COMMAND_SINGLE, rest));
                return;
            default:
                printMenu();
                return;
            }
        }
    }

    int runClientApp(const KiottyNetAddress& address, const std::string& where)
    {
        KiottyNetContextResult context = KiottyNet_initializeContext();
        if (context.code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("initializeContext failed, code " + std::to_string(context.code));
            return 1;
        }

        KiottyNetClientConfiguration configuration;
        KiottyNet_initializeClientConfiguration(&configuration);

        KiottyNetEventClientResult created = KiottyNet_createEventClient(context.handle, &configuration);
        if (created.code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("createEventClient failed, code " + std::to_string(created.code));
            KiottyNet_releaseContext(context.handle);
            return 1;
        }
        KiottyNetEventClientHandle client = created.handle;

        ClientState state;
        state.next_request_id = 0;
        state.fetch_left      = 0;
        state.events_received = 0;
        state.phase           = READ_HEADER;

        std::atomic<bool> running(true);

        std::thread io([&] {
            KiottyNetCode code = KiottyNet_runEventClient(client, &address, nullptr, &handler, &state);
            running = false;
            printLine("client loop ended, code " + std::to_string(code) + ". press Enter to exit");
        });

        KiottyNetCode started = KiottyNet_awaitEventClient(client, 5000);
        int           result  = 0;
        if (started != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("client did not start on " + where + ", code " + std::to_string(started));
            result = 1;
        }
        else
        {
            printLine("health check client on " + where);
            printMenu();
            std::string line;
            while (running && std::getline(std::cin, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    continue;
                }
                if (line == "q")
                {
                    break;
                }
                runCommand(state, client, line);
            }
        }

        KiottyNet_closeEventClient(client);
        io.join();
        KiottyNet_destroyEventClient(context.handle, client);
        KiottyNet_releaseContext(context.handle);
        return result;
    }
}
