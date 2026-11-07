#if !defined(KIOTTY_TEST_TLS_H)
#define KIOTTY_TEST_TLS_H

#include "base/tls/kiotty_tls_engine.h"
#include "base/tls/kiotty_tls_profile.h"
#include "core/kiotty_stage.h"
#include "data/datasource/kiotty_memory_limits.h"

#include <cstring>
#include <string>
#include <vector>

namespace kiotty_test
{
    using kiotty::net::implementation::StageBase;
    using kiotty::net::implementation::TlsEngine;
    using kiotty::net::implementation::TlsProfile;
    using kiotty::net::implementation::TlsStep;

    inline std::string certPath(const char* name)
    {
        std::string path(KIOTTY_TEST_CERT_DIR);
        path += "/";
        path += name;
        return path;
    }

    template <size_t N>
    inline void copyText(char (&target)[N], const std::string& text)
    {
        std::memset(target, 0, N);
        size_t length = text.size() < N - 1 ? text.size() : N - 1;
        std::memcpy(target, text.data(), length);
    }

    inline KiottyNetTlsServerConfiguration serverConfig(const char* cert = "server.pem", const char* key = "server.key")
    {
        KiottyNetTlsServerConfiguration config;
        std::memset(&config, 0, sizeof(config));
        config.versions = KIOTTY_NET_TLS_VERSION_V12 | KIOTTY_NET_TLS_VERSION_V13;
        config.v12_cipher_suites = 0xF;
        config.v13_cipher_suites = 0x1F;
        copyText(config.identity.cert_file, certPath(cert));
        copyText(config.identity.key_file, certPath(key));
        config.client_authentication = KIOTTY_NET_CLIENT_AUTHENTICATION_NONE;
        return config;
    }

    inline KiottyNetTlsClientConfiguration clientConfig(const char* trust = "ca.pem")
    {
        KiottyNetTlsClientConfiguration config;
        std::memset(&config, 0, sizeof(config));
        config.versions = KIOTTY_NET_TLS_VERSION_V12 | KIOTTY_NET_TLS_VERSION_V13;
        config.v12_cipher_suites = 0xF;
        config.v13_cipher_suites = 0x1F;
        copyText(config.trust_store_path, certPath(trust));
        return config;
    }

    inline void put(StageBase& stage, const std::string& bytes)
    {
        char* target = stage.provider().prepare(bytes.size());
        if (target == nullptr)
        {
            return;
        }
        std::memcpy(target, bytes.data(), bytes.size());
        stage.provider().commit(bytes.size());
    }

    inline std::string take(StageBase& stage)
    {
        size_t readable = stage.consumer().readable();
        std::string bytes(stage.consumer().peek(), readable);
        stage.consumer().commit(readable);
        stage.compact();
        return bytes;
    }

    inline size_t move(TlsEngine& from, TlsEngine& to, size_t limit = static_cast<size_t>(-1))
    {
        StageBase& source = from.cipherSend();
        StageBase& target = to.cipherReceive();
        size_t length = source.consumer().readable();
        size_t room = target.provider().writable();
        if (length > room)
        {
            length = room;
        }
        if (length > limit)
        {
            length = limit;
        }
        if (length == 0)
        {
            return 0;
        }
        std::memcpy(target.provider().prepare(length), source.consumer().peek(), length);
        target.provider().commit(length);
        source.consumer().commit(length);
        return length;
    }

    class TlsPump
    {
    public:
        explicit TlsPump(size_t capacity = kiotty::net::implementation::MemoryLimits::RECEIVE_CAPACITY)
            : server()
            , client()
        {
            server.limit(capacity, capacity);
            client.limit(capacity, capacity);
        }

        KiottyNetCode open(TlsProfile& server_profile, TlsProfile& client_profile, const char* hostname = "localhost")
        {
            server_steps.clear();
            client_steps.clear();
            KiottyNetCode code = server.open(server_profile, nullptr);
            if (code != KIOTTY_NET_CODE_SUCCESS)
            {
                return code;
            }
            return client.open(client_profile, hostname);
        }

        int handshake(int limit = 20)
        {
            int rounds = 0;
            while (rounds < limit && !finished())
            {
                ++rounds;
                stepOne(client, client_steps);
                move(client, server);
                stepOne(server, server_steps);
                move(server, client);
            }
            return rounds;
        }

        bool done() const
        {
            return last(server_steps) == TlsStep::DONE && last(client_steps) == TlsStep::DONE;
        }

        bool failed() const
        {
            return last(server_steps) == TlsStep::FAILED || last(client_steps) == TlsStep::FAILED;
        }

        TlsEngine             server;
        TlsEngine             client;
        std::vector<TlsStep>  server_steps;
        std::vector<TlsStep>  client_steps;

    private:
        static TlsStep last(const std::vector<TlsStep>& steps)
        {
            if (steps.empty())
            {
                return TlsStep::RECEIVE;
            }
            return steps.back();
        }

        bool finished() const
        {
            return done() || failed();
        }

        static void stepOne(TlsEngine& engine, std::vector<TlsStep>& steps)
        {
            TlsStep previous = last(steps);
            if (previous == TlsStep::DONE || previous == TlsStep::FAILED)
            {
                return;
            }
            steps.push_back(engine.handshake());
        }
    };
}

#endif
