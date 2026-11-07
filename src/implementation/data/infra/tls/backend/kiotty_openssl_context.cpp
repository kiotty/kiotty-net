#if KIOTTY_NET_USE_TLS

#include "data/infra/tls/backend/kiotty_openssl_context.h"

#include "core/kiotty_span.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <fstream>

namespace kiotty::net::implementation
{
    namespace
    {
        struct SuiteName
        {
            uint32_t    bit;
            const char* name;
        };

        struct ProtocolSelection
        {
            uint32_t versions;
            uint32_t v12_suites;
            uint32_t v13_suites;
        };

        const SuiteName V12_SUITES[] =
        {
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_AES256_GCM_SHA384,
              "ECDHE-ECDSA-AES256-GCM-SHA384" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_AES256_GCM_SHA384,
              "ECDHE-RSA-AES256-GCM-SHA384" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_CHACHA20_POLY1305,
              "ECDHE-ECDSA-CHACHA20-POLY1305" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_CHACHA20_POLY1305,
              "ECDHE-RSA-CHACHA20-POLY1305" }
        };

        const SuiteName V13_SUITES[] =
        {
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_GCM_SHA256, "TLS_AES_128_GCM_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_256_GCM_SHA384, "TLS_AES_256_GCM_SHA384" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_CHACHA20_POLY1305_SHA256,
              "TLS_CHACHA20_POLY1305_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM_SHA256, "TLS_AES_128_CCM_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM8_SHA256, "TLS_AES_128_CCM_8_SHA256" }
        };

        const Span<const SuiteName> V12_SUITE_NAMES{
            V12_SUITES, sizeof(V12_SUITES) / sizeof(SuiteName) };
        const Span<const SuiteName> V13_SUITE_NAMES{
            V13_SUITES, sizeof(V13_SUITES) / sizeof(SuiteName) };

        const size_t SUITE_LIST_BYTES = 256;
        const size_t SEPARATOR_AND_TERMINATOR_BYTES = 2;
        const long   MAX_CERT_LIST_BYTES = 16 * 1024;

        typedef char TrustStorePath[KIOTTY_NET_MAX_PATH_LENGTH];

        KiottyNetCode configureProtocol(SSL_CTX* context, ProtocolSelection protocol);
        KiottyNetCode applyV12Suites(SSL_CTX* context, uint32_t bits);
        KiottyNetCode applyV13Suites(SSL_CTX* context, uint32_t bits);
        bool          buildSuiteList(Span<char> list, uint32_t bits, Span<const SuiteName> suites);
        void          restrictFeatures(SSL_CTX* context);
        KiottyNetCode loadIdentity(SSL_CTX* context, const KiottyNetTlsIdentity& identity);
        bool          identityTerminated(const KiottyNetTlsIdentity& identity);
        bool          identityFilesExist(const KiottyNetTlsIdentity& identity);
        int           useIdentityFiles(SSL_CTX* context, const KiottyNetTlsIdentity& identity);
        int           passwordCallback(char* buffer, int size, int writing, void* user);
        KiottyNetCode loadClientTrust(SSL_CTX* context, const TrustStorePath& path);
        KiottyNetCode loadTrustStore(SSL_CTX* context, const TrustStorePath& path);
        void          loadDefaultTrustStore(SSL_CTX* context);
        bool          terminated(Span<const char> text);
        bool          fileExists(const char* path);
        bool          pathExists(const char* path);
    }

    KiottyNetCode prepareServerContext(SSL_CTX* context,
                                       const KiottyNetTlsServerConfiguration& config)
    {
        const ProtocolSelection protocol{
            config.versions, config.v12_cipher_suites, config.v13_cipher_suites };
        const KiottyNetCode configured = configureProtocol(context, protocol);
        if (configured != KIOTTY_NET_CODE_SUCCESS) { return configured; }
        const KiottyNetCode loaded = loadIdentity(context, config.identity);
        if (loaded != KIOTTY_NET_CODE_SUCCESS) { return loaded; }
        const KiottyNetClientAuthentication authentication = config.client_authentication;
        const bool authenticates = authentication != KIOTTY_NET_CLIENT_AUTHENTICATION_NONE;
        if (!authenticates) { return KIOTTY_NET_CODE_SUCCESS; }
        return loadTrustStore(context, config.trust_store_path);
    }

    KiottyNetCode prepareClientContext(SSL_CTX* context,
                                       const KiottyNetTlsClientConfiguration& config)
    {
        const ProtocolSelection protocol{
            config.versions, config.v12_cipher_suites, config.v13_cipher_suites };
        const KiottyNetCode configured = configureProtocol(context, protocol);
        if (configured != KIOTTY_NET_CODE_SUCCESS) { return configured; }
        const KiottyNetCode trusted = loadClientTrust(context, config.trust_store_path);
        if (trusted != KIOTTY_NET_CODE_SUCCESS) { return trusted; }
        const bool has_identity = config.identity.cert_file[0] != '\0';
        if (!has_identity) { return KIOTTY_NET_CODE_SUCCESS; }
        return loadIdentity(context, config.identity);
    }

    int clientVerifyMode(KiottyNetClientAuthentication authentication)
    {
        switch (authentication)
        {
        case KIOTTY_NET_CLIENT_AUTHENTICATION_NONE:
            return SSL_VERIFY_NONE;
        case KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL:
            return SSL_VERIFY_PEER;
        case KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED:
            return SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
        return SSL_VERIFY_NONE;
    }

    namespace
    {
        KiottyNetCode configureProtocol(SSL_CTX* context, ProtocolSelection protocol)
        {
            const bool v12 = (protocol.versions & KIOTTY_NET_TLS_VERSION_V12) != 0;
            const bool v13 = (protocol.versions & KIOTTY_NET_TLS_VERSION_V13) != 0;
            if (!v12 && !v13) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const int minimum = v12 ? TLS1_2_VERSION : TLS1_3_VERSION;
            const int maximum = v13 ? TLS1_3_VERSION : TLS1_2_VERSION;
            SSL_CTX_set_min_proto_version(context, minimum);
            SSL_CTX_set_max_proto_version(context, maximum);
            const uint32_t v12_suites = v12 ? protocol.v12_suites : 0;
            const KiottyNetCode v12_applied = applyV12Suites(context, v12_suites);
            if (v12_applied != KIOTTY_NET_CODE_SUCCESS) { return v12_applied; }
            const uint32_t v13_suites = v13 ? protocol.v13_suites : 0;
            const KiottyNetCode v13_applied = applyV13Suites(context, v13_suites);
            if (v13_applied != KIOTTY_NET_CODE_SUCCESS) { return v13_applied; }
            restrictFeatures(context);
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode applyV12Suites(SSL_CTX* context, uint32_t bits)
        {
            if (bits == 0) { return KIOTTY_NET_CODE_SUCCESS; }
            char list[SUITE_LIST_BYTES]{};
            const Span<char> target{ list, SUITE_LIST_BYTES };
            const bool built = buildSuiteList(target, bits, V12_SUITE_NAMES);
            if (!built) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const int applied = SSL_CTX_set_cipher_list(context, list);
            if (applied != 1) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode applyV13Suites(SSL_CTX* context, uint32_t bits)
        {
            if (bits == 0) { return KIOTTY_NET_CODE_SUCCESS; }
            char list[SUITE_LIST_BYTES]{};
            const Span<char> target{ list, SUITE_LIST_BYTES };
            const bool built = buildSuiteList(target, bits, V13_SUITE_NAMES);
            if (!built) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const int applied = SSL_CTX_set_ciphersuites(context, list);
            if (applied != 1) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        bool buildSuiteList(Span<char> list, uint32_t bits, Span<const SuiteName> suites)
        {
            size_t used = 0;
            for (const SuiteName& suite : suites)
            {
                if ((bits & suite.bit) == 0) { continue; }
                const size_t length = strnlen(suite.name, SUITE_LIST_BYTES);
                if (used + length + SEPARATOR_AND_TERMINATOR_BYTES > list.length) { return false; }
                if (used > 0)
                {
                    list[used] = ':';
                    ++used;
                }
                const Span<char> target = list.skip(used);
                const bool copied = target.copyFrom(Span<const char>{ suite.name, length });
                if (!copied) { return false; }
                used += length;
            }
            list[used] = '\0';
            return true;
        }

        void restrictFeatures(SSL_CTX* context)
        {
            SSL_CTX_set_options(context, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
            SSL_CTX_set_mode(context, SSL_MODE_RELEASE_BUFFERS);
            SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
            SSL_CTX_set_num_tickets(context, 0);
            SSL_CTX_set_max_cert_list(context, MAX_CERT_LIST_BYTES);
        }

        KiottyNetCode loadIdentity(SSL_CTX* context, const KiottyNetTlsIdentity& identity)
        {
            const bool is_terminated = identityTerminated(identity);
            if (!is_terminated) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const bool is_found = identityFilesExist(identity);
            if (!is_found) { return KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND; }
            const int keyed = useIdentityFiles(context, identity);
            if (keyed != 1) { return KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID; }
            const int matched = SSL_CTX_check_private_key(context);
            if (matched != 1) { return KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        bool identityTerminated(const KiottyNetTlsIdentity& identity)
        {
            const Span<const char> certificate{ identity.cert_file, sizeof(identity.cert_file) };
            const Span<const char> key{ identity.key_file, sizeof(identity.key_file) };
            const Span<const char> password{
                identity.key_password, sizeof(identity.key_password) };
            const bool is_certificate_terminated = terminated(certificate);
            const bool is_key_terminated = terminated(key);
            const bool is_password_terminated = terminated(password);
            return is_certificate_terminated && is_key_terminated && is_password_terminated;
        }

        bool identityFilesExist(const KiottyNetTlsIdentity& identity)
        {
            const bool certificate = fileExists(identity.cert_file);
            if (!certificate) { return false; }
            return fileExists(identity.key_file);
        }

        int useIdentityFiles(SSL_CTX* context, const KiottyNetTlsIdentity& identity)
        {
            void* password = const_cast<char*>(identity.key_password);
            SSL_CTX_set_default_passwd_cb(context, passwordCallback);
            SSL_CTX_set_default_passwd_cb_userdata(context, password);
            const int certified = SSL_CTX_use_certificate_chain_file(context, identity.cert_file);
            int keyed = 0;
            if (certified == 1)
            {
                keyed = SSL_CTX_use_PrivateKey_file(context, identity.key_file, SSL_FILETYPE_PEM);
            }
            SSL_CTX_set_default_passwd_cb_userdata(context, nullptr);
            return keyed;
        }

        int passwordCallback(char* buffer, int size, int writing, void* user)
        {
            (void)writing;
            if (user == nullptr || size <= 0) { return 0; }
            const char* password = static_cast<const char*>(user);
            const size_t room = static_cast<size_t>(size);
            const size_t stored = strnlen(password, KIOTTY_NET_MAX_PASSWORD_LENGTH);
            const size_t length = stored < room ? stored : room;
            const Span<char> target{ buffer, room };
            const bool copied = target.copyFrom(Span<const char>{ password, length });
            if (!copied) { return 0; }
            return static_cast<int>(length);
        }

        KiottyNetCode loadClientTrust(SSL_CTX* context, const TrustStorePath& path)
        {
            const bool has_path = path[0] != '\0';
            if (has_path)
            {
                return loadTrustStore(context, path);
            }
            loadDefaultTrustStore(context);
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode loadTrustStore(SSL_CTX* context, const TrustStorePath& path)
        {
            const bool is_terminated = terminated(Span<const char>{ path, sizeof(path) });
            if (!is_terminated) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const bool is_found = pathExists(path);
            if (!is_found) { return KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND; }
            const int file_loaded = SSL_CTX_load_verify_file(context, path);
            if (file_loaded == 1) { return KIOTTY_NET_CODE_SUCCESS; }
            const int directory_loaded = SSL_CTX_load_verify_dir(context, path);
            if (directory_loaded != 1) { return KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID; }
            ERR_clear_error();
            return KIOTTY_NET_CODE_SUCCESS;
        }

        void loadDefaultTrustStore(SSL_CTX* context)
        {
            SSL_CTX_set_default_verify_paths(context);
#if defined(_WIN32)
            SSL_CTX_load_verify_store(context, "org.openssl.winstore://");
#endif
            ERR_clear_error();
        }

        bool terminated(Span<const char> text)
        {
            const size_t length = strnlen(text.data, text.length);
            return length < text.length;
        }

        bool fileExists(const char* path)
        {
            std::ifstream file(path, std::ios::binary);
            return file.is_open();
        }

        bool pathExists(const char* path)
        {
#if defined(_WIN32)
            struct _stat64 info{};
            return _stat64(path, &info) == 0;
#else
            struct stat info{};
            return stat(path, &info) == 0;
#endif
        }
    }
}

#endif
