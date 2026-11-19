#include "kiotty_net_base.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    const size_t   kMaxLine          = 1024;
    const size_t   kMaxPending       = 64;
    const uint32_t kPollMilliseconds = 100;

    std::mutex print_lock;

    void printLine(const std::string& line)
    {
        std::lock_guard<std::mutex> guard(print_lock);
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    std::string trimLine(KiottyNetSpan data)
    {
        std::string text(data.data, data.length);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        {
            text.pop_back();
        }
        return text;
    }

    struct ChatServer;

    struct Peer
    {
        ChatServer*               server;
        int64_t                   id;
        KiottyNetConnectionHandle connection;
        std::deque<std::string>   pending;
    };

    struct ChatServer
    {
        std::mutex               lock;
        int64_t                  next_id;
        std::map<int64_t, Peer*> peers;
    };

    KiottyNetConnectionAction nextRead()
    {
        KiottyNetSpan delimiter = { "\n", 1 };
        return KiottyNet_thenReadUntil(delimiter, kMaxLine, kPollMilliseconds);
    }

    void flushPending(Peer& peer)
    {
        std::lock_guard<std::mutex> guard(peer.server->lock);
        while (!peer.pending.empty())
        {
            const std::string& message = peer.pending.front();
            KiottyNetSpan      data    = { message.data(), message.size() };
            KiottyNetCode      code    = KiottyNet_notifyConnection(peer.connection, data);
            if (code == KIOTTY_NET_CODE_BUFFER_FULL)
            {
                return;
            }
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                printLine("[" + std::to_string(peer.id) + "] send failed, code " + std::to_string(code));
                peer.pending.clear();
                return;
            }
            printLine("[" + std::to_string(peer.id) + "] sent: " + message.substr(0, message.size() - 1));
            peer.pending.pop_front();
        }
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        ChatServer& server = *static_cast<ChatServer*>(app_context);
        Peer*       peer   = new Peer();
        peer->server       = &server;
        {
            std::lock_guard<std::mutex> guard(server.lock);
            server.next_id += 1;
            peer->id = server.next_id;
            server.peers[peer->id] = peer;
        }
        printLine("[" + std::to_string(peer->id) + "] connected from " + info.hostname + ":" + std::to_string(info.port));
        return KiottyNet_acceptConnection(peer);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection)
    {
        Peer& peer = *static_cast<Peer*>(connection_context);
        {
            std::lock_guard<std::mutex> guard(peer.server->lock);
            peer.connection = connection;
        }
        flushPending(peer);
        return nextRead();
    }

    KiottyNetConnectionAction onRead(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle,
        KiottyNetSpan data)
    {
        Peer& peer = *static_cast<Peer*>(connection_context);
        printLine("[" + std::to_string(peer.id) + "] " + trimLine(data));
        flushPending(peer);
        return nextRead();
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, size_t)
    {
        return nextRead();
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle)
    {
        Peer& peer = *static_cast<Peer*>(connection_context);
        flushPending(peer);
        return nextRead();
    }

    void onWorkStarted(KiottyNetConnectionContext, KiottyNetConnectionStream stream)
    {
        KiottyNetConnectionAction then = KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS);
        KiottyNet_closeConnectionStream(stream, then);
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetCode reason)
    {
        ChatServer& server = *static_cast<ChatServer*>(app_context);
        Peer*       peer   = static_cast<Peer*>(connection_context);
        size_t      left   = 0;
        {
            std::lock_guard<std::mutex> guard(server.lock);
            server.peers.erase(peer->id);
            left = peer->pending.size();
        }
        printLine("[" + std::to_string(peer->id) + "] disconnected, code " + std::to_string(reason) + ", dropped " + std::to_string(left) + " pending");
        delete peer;
    }

    void onError(KiottyNetAppContext, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        printLine(std::string("error ") + info.hostname + ":" + std::to_string(info.port) + " code " + std::to_string(code) + " " + message);
    }

    void listPeers(ChatServer& server)
    {
        std::lock_guard<std::mutex> guard(server.lock);
        printLine(std::to_string(server.peers.size()) + " client(s)");
        for (const auto& entry : server.peers)
        {
            printLine("  " + std::to_string(entry.first) + " pending " + std::to_string(entry.second->pending.size()));
        }
    }

    void broadcast(KiottyNetServerHandle server, const std::string& message)
    {
        std::string   line = message + "\n";
        KiottyNetSpan data = { line.data(), line.size() };
        KiottyNetCode code = KiottyNet_broadcastServer(server, data);
        if (code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("broadcast failed, code " + std::to_string(code));
            return;
        }
        printLine("broadcast: " + message);
    }

    void enqueue(ChatServer& chat, KiottyNetServerHandle server, const std::string& line)
    {
        size_t space = line.find(' ');
        if (space == std::string::npos || space == 0)
        {
            printLine("usage: <id> <message>, 0 <message> for all, /list, /quit");
            return;
        }
        std::string id_text = line.substr(0, space);
        std::string message = line.substr(space + 1);
        char*       end     = nullptr;
        long long   id      = std::strtoll(id_text.c_str(), &end, 10);
        if (end == nullptr || *end != '\0')
        {
            printLine("not an id: " + id_text);
            return;
        }
        if (message.size() + 1 > kMaxLine)
        {
            printLine("message too long");
            return;
        }
        if (id == 0)
        {
            broadcast(server, message);
            return;
        }
        std::lock_guard<std::mutex> guard(chat.lock);
        auto found = chat.peers.find(id);
        if (found == chat.peers.end())
        {
            printLine("no client " + id_text);
            return;
        }
        Peer& peer = *found->second;
        if (peer.pending.size() >= kMaxPending)
        {
            printLine("queue of " + id_text + " is full");
            return;
        }
        peer.pending.push_back(message + "\n");
        printLine("queued to " + id_text + " (" + std::to_string(peer.pending.size()) + " pending)");
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

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    uint16_t    port = 9000;
    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    if (argc > 2)
    {
        host = argv[2];
    }

    KiottyNetContextResult context = KiottyNet_initializeContext();
    if (context.code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "initializeContext failed, code %d\n", static_cast<int>(context.code));
        return 1;
    }

    KiottyNetServerConfiguration configuration;
    KiottyNet_initializeServerConfiguration(&configuration);
    KiottyNet_configureServerMaxClients(&configuration, 16);

    KiottyNetServerResult created = KiottyNet_createServer(context.handle, &configuration);
    if (created.code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "createServer failed, code %d\n", static_cast<int>(created.code));
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    KiottyNetServerHandle server = created.handle;

    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, host, port);

    ChatServer chat;
    chat.next_id = 0;

    std::thread io([&] {
        KiottyNetCode code = KiottyNet_runServer(server, &address, nullptr, &handler, &chat);
        printLine("server loop ended, code " + std::to_string(code));
    });

    KiottyNetCode started = KiottyNet_awaitServer(server, 3000);
    if (started != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "server did not start, code %d\n", static_cast<int>(started));
        KiottyNet_closeServer(server);
        io.join();
        KiottyNet_destroyServer(context.handle, server);
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    printLine(std::string("listening on ") + host + ":" + std::to_string(port) + ". type <id> <message>, 0 <message> for all, /list, /quit");

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        if (line == "/quit")
        {
            break;
        }
        if (line == "/list")
        {
            listPeers(chat);
            continue;
        }
        enqueue(chat, server, line);
    }

    KiottyNet_closeServer(server);
    io.join();
    KiottyNet_destroyServer(context.handle, server);
    KiottyNet_releaseContext(context.handle);
    return 0;
}
