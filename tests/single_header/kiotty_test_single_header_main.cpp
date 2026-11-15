#include "kiotty_test_single_header_names.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

extern "C" int kiottyTestSingleHeaderC(void);
bool           kiottyTestSingleHeaderSecond();

namespace
{
    const uint32_t WAIT_MS = 5000;
    const size_t   TEXT_CAPACITY = 64;
    const size_t   MAX_READS = 64;
    const char     HELLO[] = "hello";
    const char     PING[] = "ping";

#if defined(_WIN32)
    const char PIPE_FORMAT[] = "ksh_%08x.sock";
#else
    const char PIPE_FORMAT[] = "/tmp/ksh_%08x.sock";
#endif

    struct Received
    {
        std::mutex              mutex;
        std::condition_variable changed;
        char                    text[TEXT_CAPACITY];
        bool                    has_text;
    };

    bool fail(const char* step)
    {
        std::fprintf(stderr, "single header smoke failed: %s\n", step);
        return false;
    }

    smoke::Span spanOf(const char* text)
    {
        const smoke::Span span = { text, std::strlen(text) };
        return span;
    }

    bool isText(const char* text, size_t length, const char* expected)
    {
        return length == std::strlen(expected) && std::memcmp(text, expected, length) == 0;
    }

    void onHello(smoke::AppContext, const smoke::HttpRequest*, smoke::HttpResponseHandle response)
    {
        (void)smoke::setHttpBody(response, smoke::TEXT_PLAIN, spanOf(HELLO));
    }

    smoke::WebSocketContext onSocketOpen(smoke::AppContext app_context, smoke::WebSocketHandle)
    {
        return app_context;
    }

    void onSocketMessage(smoke::WebSocketContext,
                         smoke::WebSocketHandle websocket,
                         smoke::WebSocketMessage message)
    {
        (void)smoke::sendWebSocket(websocket, message.opcode, message.data);
    }

    void onClientMessage(smoke::AppContext app_context, smoke::WebSocketMessage message)
    {
        Received& received = *static_cast<Received*>(app_context);
        {
            const std::lock_guard<std::mutex> lock(received.mutex);
            const size_t length = message.data.length < TEXT_CAPACITY - 1
                                      ? message.data.length
                                      : TEXT_CAPACITY - 1;
            std::memcpy(received.text, message.data.data, length);
            received.text[length] = '\0';
            received.has_text = true;
        }
        received.changed.notify_all();
    }

    bool pipeAddress(smoke::Address& address)
    {
        std::random_device device;
        const uint64_t     ticks = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const unsigned int salt = static_cast<unsigned int>(device() ^ static_cast<uint32_t>(ticks));
        char name[TEXT_CAPACITY];
        (void)std::snprintf(name, sizeof(name), PIPE_FORMAT, salt);
        return smoke::initializePipeAddress(&address, name) == smoke::SUCCESS;
    }

    bool exchangeHttp(smoke::Context context, const smoke::Address& address)
    {
        smoke::ClientConfiguration configuration;
        if (smoke::initializeClientConfiguration(&configuration) != smoke::SUCCESS)
        {
            return fail("client configuration");
        }
        smoke::HttpClient client(context, configuration, nullptr);
        if (!client || client.connect(address) != smoke::SUCCESS)
        {
            return fail("http client connect");
        }
        const smoke::HttpReply reply = client.get("/hello", WAIT_MS);
        if (reply.code != smoke::SUCCESS || reply.status != 200)
        {
            return fail("http reply");
        }
        char   body[TEXT_CAPACITY];
        size_t length = 0;
        for (size_t reads = 0; reads < MAX_READS && length < sizeof(body); ++reads)
        {
            const smoke::Result got = client.readBody(body + length, sizeof(body) - length, WAIT_MS);
            if (got.code != smoke::SUCCESS || got.bytes <= 0)
            {
                break;
            }
            length += static_cast<size_t>(got.bytes);
        }
        return isText(body, length, HELLO) || fail("http body");
    }

    bool exchangeWebSocket(smoke::Context context,
                           const smoke::Address& address,
                           const smoke::HttpLimits& limits)
    {
        smoke::ClientConfiguration configuration;
        if (smoke::initializeClientConfiguration(&configuration) != smoke::SUCCESS)
        {
            return fail("websocket configuration");
        }
        smoke::WebSocketClient client(context, configuration, &limits);
        if (!client || client.onMessage(onClientMessage) != smoke::SUCCESS)
        {
            return fail("websocket client");
        }
        Received received;
        received.has_text = false;
        std::thread io([&client, &address, &received]() {
            (void)client.run(address, "/echo", &received);
        });
        bool is_echoed = client.await(WAIT_MS) == smoke::SUCCESS
                         && client.send(smoke::TEXT, spanOf(PING)) == smoke::SUCCESS;
        if (is_echoed)
        {
            std::unique_lock<std::mutex> lock(received.mutex);
            const std::chrono::milliseconds limit(WAIT_MS);
            is_echoed = received.changed.wait_for(lock, limit, [&received]() {
                return received.has_text;
            });
            is_echoed = is_echoed && isText(received.text, std::strlen(received.text), PING);
        }
        (void)client.bye();
        client.close();
        io.join();
        return is_echoed || fail("websocket echo");
    }

    bool serve(smoke::Context context)
    {
        smoke::ServerConfiguration configuration;
        smoke::HttpLimits          limits;
        const bool is_configured = smoke::initializeServerConfiguration(&configuration) == smoke::SUCCESS
                                   && smoke::initializeHttpLimits(&limits) == smoke::SUCCESS;
        if (!is_configured)
        {
            return fail("server configuration");
        }
        smoke::HttpServer server(context, configuration, limits);
        const bool is_routed = static_cast<bool>(server)
                               && server.get("/hello", onHello) == smoke::SUCCESS
                               && smoke::WebSocketRoute(server, "/echo")
                                          .onOpen(onSocketOpen)
                                          .onMessage(onSocketMessage)
                                          .build()
                                      == smoke::SUCCESS;
        smoke::Address address;
        if (!is_routed || !pipeAddress(address))
        {
            return fail("server routes");
        }
        std::thread runner([&server, &address]() { (void)server.run(address, nullptr); });
        bool is_working = server.await(WAIT_MS) == smoke::SUCCESS || fail("server start");
        is_working = is_working && exchangeHttp(context, address);
        is_working = is_working && exchangeWebSocket(context, address, limits);
        server.close();
        runner.join();
        return is_working;
    }
}

int main()
{
    if (kiottyTestSingleHeaderC() != 1 || !kiottyTestSingleHeaderSecond())
    {
        (void)fail("header units");
        return 1;
    }
    const smoke::ContextResult opened = smoke::initializeContext();
    if (opened.code != smoke::SUCCESS)
    {
        (void)fail("context");
        return 1;
    }
    const bool is_working = serve(opened.handle);
    smoke::releaseContext(opened.handle);
    if (!is_working)
    {
        return 1;
    }
    std::printf("single header smoke passed\n");
    return 0;
}
