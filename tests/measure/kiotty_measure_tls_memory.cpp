#include "support/kiotty_test_tls.h"

#include <openssl/crypto.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using kiotty::Stage;
using kiotty::StageBase;
using kiotty::net::DecodeResult;
using kiotty::net::TlsEngine;
using kiotty::net::TlsProfile;
using kiotty::net::TlsStep;
using kiotty_test::TlsPump;
using kiotty_test::copyText;
using kiotty_test::move;
using kiotty_test::take;

namespace
{
    enum Tag
    {
        TAG_GLOBAL,
        TAG_WARM,
        TAG_SERVER_PROFILE,
        TAG_CLIENT_PROFILE,
        TAG_SERVER_CONNECTION,
        TAG_CLIENT_CONNECTION,
        TAG_COUNT
    };

    const char* const TAG_NAMES[TAG_COUNT] = { "global", "warm", "server_profile", "client_profile", "server_connection", "client_connection" };

    const size_t HEADER_BYTES = 16;
    const size_t CLASS_BYTES[] =
    {
        32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144,
        8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072
    };
    const size_t CLASS_COUNT = sizeof(CLASS_BYTES) / sizeof(CLASS_BYTES[0]);

    struct alignas(16) Header
    {
        size_t size;
        size_t tag;
    };

    static_assert(sizeof(Header) == HEADER_BYTES, "Header must be 16 bytes");

    struct Counter
    {
        size_t live;
        size_t peak;
        size_t total;
    };

    struct Ledger
    {
        Counter                   classes[CLASS_COUNT + 1];
        std::map<size_t, Counter> sizes;
        size_t                    live_bytes;
        size_t                    peak_bytes;
    };

    std::mutex g_lock;
    Ledger     g_ledgers[TAG_COUNT];
    size_t     g_tag = TAG_GLOBAL;

    size_t classOf(size_t size)
    {
        for (size_t index = 0; index < CLASS_COUNT; ++index)
        {
            if (size <= CLASS_BYTES[index])
            {
                return index;
            }
        }
        return CLASS_COUNT;
    }

    size_t classBytes(size_t index)
    {
        return CLASS_BYTES[index];
    }

    size_t g_profile_blocks[CLASS_COUNT + 1];
    size_t g_connection_blocks[CLASS_COUNT + 1];

    size_t ceilDivide(size_t value, size_t divisor)
    {
        return (value + divisor - 1) / divisor;
    }

    void keepWorst(size_t* worst, const Ledger& ledger, size_t divisor)
    {
        for (size_t index = 0; index <= CLASS_COUNT; ++index)
        {
            size_t blocks = ceilDivide(ledger.classes[index].peak, divisor);
            if (blocks > worst[index])
            {
                worst[index] = blocks;
            }
        }
    }

    void raise(Counter& counter)
    {
        counter.live += 1;
        counter.total += 1;
        if (counter.live > counter.peak)
        {
            counter.peak = counter.live;
        }
    }

    void record(size_t tag, size_t size)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        Ledger& ledger = g_ledgers[tag];
        raise(ledger.classes[classOf(size)]);
        raise(ledger.sizes[size]);
        ledger.live_bytes += size;
        if (ledger.live_bytes > ledger.peak_bytes)
        {
            ledger.peak_bytes = ledger.live_bytes;
        }
    }

    void forget(size_t tag, size_t size)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        Ledger& ledger = g_ledgers[tag];
        ledger.classes[classOf(size)].live -= 1;
        std::map<size_t, Counter>::iterator found = ledger.sizes.find(size);
        if (found != ledger.sizes.end())
        {
            found->second.live -= 1;
        }
        ledger.live_bytes -= size;
    }

    void* countedAllocate(size_t size)
    {
        Header* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
        if (header == nullptr)
        {
            return nullptr;
        }
        header->size = size;
        header->tag = g_tag;
        record(header->tag, size);
        return header + 1;
    }

    void countedRelease(void* block)
    {
        if (block == nullptr)
        {
            return;
        }
        Header* header = static_cast<Header*>(block) - 1;
        forget(header->tag, header->size);
        std::free(header);
    }

    void* hookAllocate(size_t size, const char* file, int line)
    {
        (void)file;
        (void)line;
        return countedAllocate(size);
    }

    void* hookReallocate(void* block, size_t size, const char* file, int line)
    {
        (void)file;
        (void)line;
        if (block == nullptr)
        {
            return countedAllocate(size);
        }
        if (size == 0)
        {
            countedRelease(block);
            return nullptr;
        }
        void* moved = countedAllocate(size);
        if (moved == nullptr)
        {
            return nullptr;
        }
        Header* header = static_cast<Header*>(block) - 1;
        size_t kept = header->size < size ? header->size : size;
        std::memcpy(moved, block, kept);
        countedRelease(block);
        return moved;
    }

    void hookRelease(void* block, const char* file, int line)
    {
        (void)file;
        (void)line;
        countedRelease(block);
    }

    const bool g_counting = CRYPTO_set_mem_functions(hookAllocate, hookReallocate, hookRelease) == 1;

    void resetPeaks(size_t tag)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        Ledger& ledger = g_ledgers[tag];
        for (size_t index = 0; index <= CLASS_COUNT; ++index)
        {
            ledger.classes[index].peak = ledger.classes[index].live;
            ledger.classes[index].total = 0;
        }
        for (std::map<size_t, Counter>::iterator it = ledger.sizes.begin(); it != ledger.sizes.end(); ++it)
        {
            it->second.peak = it->second.live;
            it->second.total = 0;
        }
        ledger.peak_bytes = ledger.live_bytes;
    }

    class TagScope
    {
    public:
        explicit TagScope(Tag tag)
            : _previous(g_tag)
        {
            g_tag = tag;
        }

        ~TagScope()
        {
            g_tag = _previous;
        }

    private:
        size_t _previous;
    };

    struct Scenario
    {
        const char* name;
        const char* kind;
        uint32_t    versions;
        bool        client_auth;
    };

    std::string g_cert_dir;

    std::string certFile(const char* role, const char* kind, const char* extension)
    {
        std::string path = g_cert_dir;
        path += "/";
        path += role;
        path += "_";
        path += kind;
        path += extension;
        return path;
    }

    KiottyNetTlsServerConfig serverConfigOf(const Scenario& scenario)
    {
        KiottyNetTlsServerConfig config;
        std::memset(&config, 0, sizeof(config));
        config.versions = scenario.versions;
        config.v12_cipher_suites = 0xF;
        config.v13_cipher_suites = 0x1F;
        copyText(config.identity.cert_file, certFile("server", scenario.kind, ".pem"));
        copyText(config.identity.key_file, certFile("server", scenario.kind, ".key"));
        config.client_auth = scenario.client_auth ? KIOTTY_CLIENT_AUTH_REQUIRED : KIOTTY_CLIENT_AUTH_NONE;
        if (scenario.client_auth)
        {
            copyText(config.trust_store_path, certFile("ca", scenario.kind, ".pem"));
        }
        return config;
    }

    KiottyNetTlsClientConfig clientConfigOf(const Scenario& scenario)
    {
        KiottyNetTlsClientConfig config;
        std::memset(&config, 0, sizeof(config));
        config.versions = scenario.versions;
        config.v12_cipher_suites = 0xF;
        config.v13_cipher_suites = 0x1F;
        copyText(config.trust_store_path, certFile("ca", scenario.kind, ".pem"));
        if (scenario.client_auth)
        {
            copyText(config.identity.cert_file, certFile("client", scenario.kind, ".pem"));
            copyText(config.identity.key_file, certFile("client", scenario.kind, ".key"));
        }
        return config;
    }

    size_t g_stage_bytes = KIOTTY_NET_MAX_RECV_CAPACITY;
    size_t g_message_bytes = 65536;

    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    bool stepSide(TlsEngine& engine, TlsStep& last, Tag tag)
    {
        if (last == TlsStep::DONE || last == TlsStep::FAILED)
        {
            return false;
        }
        TagScope scope(tag);
        last = engine.handshake();
        return true;
    }

    struct Pair
    {
        Pair()
            : pump(g_stage_bytes)
            , server_last(TlsStep::RECEIVE)
            , client_last(TlsStep::RECEIVE)
        {
        }

        TlsPump pump;
        TlsStep server_last;
        TlsStep client_last;
    };

    bool handshakeRound(Pair& pair)
    {
        bool client_moved = stepSide(pair.pump.client, pair.client_last, TAG_CLIENT_CONNECTION);
        move(pair.pump.client, pair.pump.server);
        bool server_moved = stepSide(pair.pump.server, pair.server_last, TAG_SERVER_CONNECTION);
        move(pair.pump.server, pair.pump.client);
        return client_moved || server_moved;
    }

    void decodeAll(TlsEngine& engine, StageBase& plain, Tag tag)
    {
        TagScope scope(tag);
        for (;;)
        {
            DecodeResult result = engine.decode(plain);
            take(plain);
            if (!result.isOk() || result.value() == 0)
            {
                return;
            }
        }
    }

    void drain(TlsEngine& from, TlsEngine& to, StageBase& plain, Tag tag)
    {
        for (;;)
        {
            size_t moved = move(from, to);
            decodeAll(to, plain, tag);
            if (moved == 0)
            {
                return;
            }
        }
    }

    bool runPairs(std::vector<std::unique_ptr<Pair>>& pairs, TlsProfile& server, TlsProfile& client)
    {
        for (size_t i = 0; i < pairs.size(); ++i)
        {
            TagScope server_scope(TAG_SERVER_CONNECTION);
            if (pairs[i]->pump.server.open(server, nullptr) != KIOTTY_SUCCESS)
            {
                return false;
            }
            TagScope client_scope(TAG_CLIENT_CONNECTION);
            if (pairs[i]->pump.client.open(client, "localhost") != KIOTTY_SUCCESS)
            {
                return false;
            }
        }
        bool moving = true;
        while (moving)
        {
            moving = false;
            for (size_t i = 0; i < pairs.size(); ++i)
            {
                if (handshakeRound(*pairs[i]))
                {
                    moving = true;
                }
            }
        }
        Stage<KIOTTY_NET_MAX_RECV_CAPACITY> plain;
        plain.limit(g_stage_bytes);
        for (size_t i = 0; i < pairs.size(); ++i)
        {
            if (pairs[i]->server_last != TlsStep::DONE || pairs[i]->client_last != TlsStep::DONE)
            {
                return false;
            }
            decodeAll(pairs[i]->pump.client, plain, TAG_CLIENT_CONNECTION);
        }
        std::string message(g_message_bytes, 'm');
        for (size_t i = 0; i < pairs.size(); ++i)
        {
            {
                TagScope scope(TAG_CLIENT_CONNECTION);
                pairs[i]->pump.client.encode(spanOf(message));
            }
            move(pairs[i]->pump.client, pairs[i]->pump.server, 8000);
            decodeAll(pairs[i]->pump.server, plain, TAG_SERVER_CONNECTION);
        }
        for (size_t i = 0; i < pairs.size(); ++i)
        {
            {
                TagScope scope(TAG_SERVER_CONNECTION);
                pairs[i]->pump.server.encode(spanOf(message));
            }
            move(pairs[i]->pump.server, pairs[i]->pump.client, 8000);
            decodeAll(pairs[i]->pump.client, plain, TAG_CLIENT_CONNECTION);
        }
        for (size_t i = 0; i < pairs.size(); ++i)
        {
            drain(pairs[i]->pump.client, pairs[i]->pump.server, plain, TAG_SERVER_CONNECTION);
            drain(pairs[i]->pump.server, pairs[i]->pump.client, plain, TAG_CLIENT_CONNECTION);
        }
        return true;
    }

    bool runScenario(const Scenario& scenario, size_t count, bool tagged)
    {
        TlsProfile server;
        TlsProfile client;
        {
            TagScope scope(tagged ? TAG_SERVER_PROFILE : TAG_WARM);
            if (server.openServer(serverConfigOf(scenario)) != KIOTTY_SUCCESS)
            {
                std::printf("server profile failed\n");
                return false;
            }
        }
        {
            TagScope scope(tagged ? TAG_CLIENT_PROFILE : TAG_WARM);
            if (client.openClient(clientConfigOf(scenario)) != KIOTTY_SUCCESS)
            {
                std::printf("client profile failed\n");
                return false;
            }
        }
        std::vector<std::unique_ptr<Pair>> pairs;
        for (size_t i = 0; i < count; ++i)
        {
            pairs.emplace_back(new Pair());
        }
        if (!tagged)
        {
            TagScope scope(TAG_WARM);
            return runPairs(pairs, server, client);
        }
        bool ok = runPairs(pairs, server, client);
        std::printf("  idle live per connection: server %zu B, client %zu B\n",
            g_ledgers[TAG_SERVER_CONNECTION].live_bytes / count,
            g_ledgers[TAG_CLIENT_CONNECTION].live_bytes / count);
        return ok;
    }

    void printLedger(Tag tag, size_t divisor)
    {
        const Ledger& ledger = g_ledgers[tag];
        std::printf("  %s: peak %zu B, live %zu B (divided by %zu)\n", TAG_NAMES[tag], ledger.peak_bytes / divisor, ledger.live_bytes / divisor, divisor);
        for (size_t index = 0; index <= CLASS_COUNT; ++index)
        {
            const Counter& counter = ledger.classes[index];
            if (counter.peak == 0 && counter.live == 0)
            {
                continue;
            }
            size_t bytes = index < CLASS_COUNT ? classBytes(index) : 0;
            std::printf("    class %7zu: peak %6zu live %6zu  peak/conn %.2f\n", bytes, counter.peak, counter.live, static_cast<double>(counter.peak) / static_cast<double>(divisor));
        }
    }

    void printLargest(Tag tag, size_t divisor)
    {
        const Ledger& ledger = g_ledgers[tag];
        std::printf("    sizes >= 1024:");
        for (std::map<size_t, Counter>::const_iterator it = ledger.sizes.begin(); it != ledger.sizes.end(); ++it)
        {
            if (it->first >= 1024 && it->second.peak > 0)
            {
                std::printf(" %zu x%.2f", it->first, static_cast<double>(it->second.peak) / static_cast<double>(divisor));
            }
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("usage: %s <cert dir> <connections> <stage bytes> <message bytes>\n", argv[0]);
        return 2;
    }
    if (!g_counting)
    {
        std::printf("CRYPTO_set_mem_functions failed\n");
        return 1;
    }
    g_cert_dir = argv[1];
    size_t count = static_cast<size_t>(std::strtoul(argv[2], nullptr, 10));
    g_stage_bytes = static_cast<size_t>(std::strtoul(argv[3], nullptr, 10));
    g_message_bytes = static_cast<size_t>(std::strtoul(argv[4], nullptr, 10));
    const Scenario scenarios[] =
    {
        { "rsa2048 tls13", "rsa2048", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, false },
        { "rsa2048 tls12", "rsa2048", KIOTTY_TLS_V12, false },
        { "rsa2048 tls13 client-auth", "rsa2048", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, true },
        { "rsa4096 tls13", "rsa4096", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, false },
        { "rsa4096 tls13 client-auth", "rsa4096", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, true },
        { "ec256 tls13", "ec256", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, false },
        { "ec256 tls12", "ec256", KIOTTY_TLS_V12, false },
        { "ec256 tls13 client-auth", "ec256", KIOTTY_TLS_V12 | KIOTTY_TLS_V13, true },
    };
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        if (!runScenario(scenarios[i], 1, false))
        {
            return 1;
        }
    }
    std::printf("== global (after warm-up of every scenario, all objects closed)\n");
    printLedger(TAG_GLOBAL, 1);
    printLedger(TAG_WARM, 1);
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        for (size_t tag = TAG_SERVER_PROFILE; tag < TAG_COUNT; ++tag)
        {
            resetPeaks(tag);
        }
        std::printf("== %s, %zu connections in lockstep, stage %zu B, message %zu B\n", scenarios[i].name, count, g_stage_bytes, g_message_bytes);
        if (!runScenario(scenarios[i], count, true))
        {
            std::printf("scenario failed\n");
            return 1;
        }
        printLedger(TAG_SERVER_PROFILE, 1);
        printLedger(TAG_CLIENT_PROFILE, 1);
        printLedger(TAG_SERVER_CONNECTION, count);
        printLargest(TAG_SERVER_CONNECTION, count);
        printLedger(TAG_CLIENT_CONNECTION, count);
        printLargest(TAG_CLIENT_CONNECTION, count);
        keepWorst(g_profile_blocks, g_ledgers[TAG_SERVER_PROFILE], 1);
        keepWorst(g_profile_blocks, g_ledgers[TAG_CLIENT_PROFILE], 1);
        keepWorst(g_connection_blocks, g_ledgers[TAG_SERVER_CONNECTION], count);
        keepWorst(g_connection_blocks, g_ledgers[TAG_CLIENT_CONNECTION], count);
    }
    std::printf("== after every scenario closed\n");
    for (size_t tag = TAG_SERVER_PROFILE; tag < TAG_COUNT; ++tag)
    {
        std::printf("  %s live %zu B\n", TAG_NAMES[tag], g_ledgers[tag].live_bytes);
    }
    std::printf("== TlsLayoutOf (Blocks::count(global, profile, connection))\n");
    size_t connection_bytes = 0;
    for (size_t index = 0; index < CLASS_COUNT; ++index)
    {
        size_t global = g_ledgers[TAG_WARM].classes[index].peak + g_ledgers[TAG_GLOBAL].classes[index].peak;
        size_t profile = g_profile_blocks[index];
        size_t connection = g_connection_blocks[index];
        if (global == 0 && profile == 0 && connection == 0)
        {
            continue;
        }
        connection_bytes += connection * CLASS_BYTES[index];
        std::printf("            BlockClass<%zu, Blocks::count(%zu, %zu, %zu)>,\n", CLASS_BYTES[index], global, profile, connection);
    }
    std::printf("  beyond the largest class: %zu blocks\n", g_connection_blocks[CLASS_COUNT] + g_ledgers[TAG_WARM].classes[CLASS_COUNT].peak);
    std::printf("  class-rounded bytes per connection: %zu\n", connection_bytes);
    return 0;
}
