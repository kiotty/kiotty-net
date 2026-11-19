#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    const size_t   kMaxLine          = 1024;
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

    KiottyNetConnectionAction nextRead()
    {
        KiottyNetSpan delimiter = { "\n", 1 };
        return KiottyNet_thenReadUntil(delimiter, kMaxLine, kPollMilliseconds);
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        printLine(std::string("connected to ") + info.hostname + ":" + std::to_string(info.port));
        return KiottyNet_acceptConnection(app_context);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext, KiottyNetConnectionHandle)
    {
        return nextRead();
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetSpan data)
    {
        printLine("server: " + trimLine(data));
        return nextRead();
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, size_t)
    {
        return nextRead();
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle)
    {
        return nextRead();
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
}

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    uint16_t    port = 9000;
    if (argc > 1)
    {
        host = argv[1];
    }
    if (argc > 2)
    {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    KiottyNetContextResult context = KiottyNet_initializeContext();
    if (context.code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "initializeContext failed, code %d\n", static_cast<int>(context.code));
        return 1;
    }

    KiottyNetClientConfiguration configuration;
    KiottyNet_initializeClientConfiguration(&configuration);

    KiottyNetEventClientResult created = KiottyNet_createEventClient(context.handle, &configuration);
    if (created.code != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "createEventClient failed, code %d\n", static_cast<int>(created.code));
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    KiottyNetEventClientHandle client = created.handle;

    KiottyNetAddress address;
    KiottyNet_initializeTcpAddress(&address, host, port);

    std::atomic<bool> running(true);

    std::thread io([&] {
        KiottyNetCode code = KiottyNet_runEventClient(client, &address, nullptr, &handler, nullptr);
        running = false;
        printLine("client loop ended, code " + std::to_string(code) + ". press Enter to exit");
    });

    KiottyNetCode started = KiottyNet_awaitEventClient(client, 5000);
    if (started != KIOTTY_NET_CODE_SUCCESS)
    {
        std::fprintf(stderr, "client did not start, code %d\n", static_cast<int>(started));
        KiottyNet_closeEventClient(client);
        io.join();
        KiottyNet_destroyEventClient(context.handle, client);
        KiottyNet_releaseContext(context.handle);
        return 1;
    }
    printLine("type a message and press Enter. /quit to exit");

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
        if (line == "/quit")
        {
            break;
        }
        if (line.size() + 1 > kMaxLine)
        {
            printLine("message too long");
            continue;
        }
        std::string   message = line + "\n";
        KiottyNetSpan data    = { message.data(), message.size() };
        KiottyNetCode code    = KiottyNet_notifyEventClient(client, data);
        if (code != KIOTTY_NET_CODE_SUCCESS)
        {
            printLine("send failed, code " + std::to_string(code));
        }
    }

    KiottyNet_closeEventClient(client);
    io.join();
    KiottyNet_destroyEventClient(context.handle, client);
    KiottyNet_releaseContext(context.handle);
    return 0;
}
