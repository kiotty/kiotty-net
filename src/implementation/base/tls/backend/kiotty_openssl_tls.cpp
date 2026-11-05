#if KIOTTY_NET_USE_TLS

#include "base/pool/kiotty_memory_layout.h"
#include "base/tls/kiotty_tls_engine.h"
#include "base/tls/kiotty_tls_memory.h"
#include "base/tls/kiotty_tls_profile.h"

#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <sys/stat.h>

#include <cassert>
#include <cstring>
#include <fstream>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            struct SuiteName
            {
                uint32_t    bit;
                const char* name;
            };

            const SuiteName V12_SUITES[] =
            {
                { KIOTTY_TLS12_ECDHE_ECDSA_AES256_GCM_SHA384, "ECDHE-ECDSA-AES256-GCM-SHA384" },
                { KIOTTY_TLS12_ECDHE_RSA_AES256_GCM_SHA384, "ECDHE-RSA-AES256-GCM-SHA384" },
                { KIOTTY_TLS12_ECDHE_ECDSA_CHACHA20_POLY1305, "ECDHE-ECDSA-CHACHA20-POLY1305" },
                { KIOTTY_TLS12_ECDHE_RSA_CHACHA20_POLY1305, "ECDHE-RSA-CHACHA20-POLY1305" }
            };

            const SuiteName V13_SUITES[] =
            {
                { KIOTTY_TLS13_AES_128_GCM_SHA256, "TLS_AES_128_GCM_SHA256" },
                { KIOTTY_TLS13_AES_256_GCM_SHA384, "TLS_AES_256_GCM_SHA384" },
                { KIOTTY_TLS13_CHACHA20_POLY1305_SHA256, "TLS_CHACHA20_POLY1305_SHA256" },
                { KIOTTY_TLS13_AES_128_CCM_SHA256, "TLS_AES_128_CCM_SHA256" },
                { KIOTTY_TLS13_AES_128_CCM8_SHA256, "TLS_AES_128_CCM_8_SHA256" }
            };

            const size_t SUITE_LIST_BYTES = 256;
            const size_t BIO_PAIR_BYTES = 4096;
            const long   MAX_CERT_LIST_BYTES = 16 * 1024;
            const int    WARM_RSA_BITS = 1024;
            const int    WARM_ROUNDS = 32;
            const char   WARM_PASSWORD[] = "kiotty warm";

            template <size_t N>
            bool terminated(const char (&text)[N])
            {
                const void* end = std::memchr(text, '\0', N);
                return end != nullptr;
            }

            bool fileExists(const char* path)
            {
                std::ifstream file(path, std::ios::binary);
                return file.is_open();
            }

            bool pathExists(const char* path)
            {
#if defined(_WIN32)
                struct _stat64 info;
                return _stat64(path, &info) == 0;
#else
                struct stat info;
                return stat(path, &info) == 0;
#endif
            }

            const char* lastErrorMessage()
            {
                unsigned long error_code = ERR_peek_last_error();
                const char* reason = ERR_reason_error_string(error_code);
                if (reason == nullptr)
                {
                    return "tls: unknown error";
                }
                return reason;
            }

            template <size_t N>
            bool buildSuiteList(char* list, uint32_t bits, const SuiteName (&suites)[N])
            {
                size_t used = 0;
                for (size_t i = 0; i < N; ++i)
                {
                    if ((bits & suites[i].bit) == 0)
                    {
                        continue;
                    }
                    size_t length = std::strlen(suites[i].name);
                    if (used + length + 2 > SUITE_LIST_BYTES)
                    {
                        return false;
                    }
                    if (used > 0)
                    {
                        list[used] = ':';
                        ++used;
                    }
                    std::memcpy(list + used, suites[i].name, length);
                    used += length;
                }
                list[used] = '\0';
                return true;
            }

            KiottyNetCode configureProtocol(SSL_CTX* context, uint32_t versions, uint32_t v12_suites, uint32_t v13_suites)
            {
                bool v12 = (versions & KIOTTY_TLS_V12) != 0;
                bool v13 = (versions & KIOTTY_TLS_V13) != 0;
                if (!v12 && !v13)
                {
                    return KIOTTY_INVALID_ARGUMENT;
                }
                int minimum = v12 ? TLS1_2_VERSION : TLS1_3_VERSION;
                int maximum = v13 ? TLS1_3_VERSION : TLS1_2_VERSION;
                SSL_CTX_set_min_proto_version(context, minimum);
                SSL_CTX_set_max_proto_version(context, maximum);

                char list[SUITE_LIST_BYTES];
                if (v12 && v12_suites != 0)
                {
                    bool built = buildSuiteList(list, v12_suites, V12_SUITES);
                    if (!built || SSL_CTX_set_cipher_list(context, list) != 1)
                    {
                        return KIOTTY_INVALID_ARGUMENT;
                    }
                }
                if (v13 && v13_suites != 0)
                {
                    bool built = buildSuiteList(list, v13_suites, V13_SUITES);
                    if (!built || SSL_CTX_set_ciphersuites(context, list) != 1)
                    {
                        return KIOTTY_INVALID_ARGUMENT;
                    }
                }

                SSL_CTX_set_options(context, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
                SSL_CTX_set_mode(context, SSL_MODE_RELEASE_BUFFERS);
                SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
                SSL_CTX_set_num_tickets(context, 0);
                SSL_CTX_set_max_cert_list(context, MAX_CERT_LIST_BYTES);
                return KIOTTY_SUCCESS;
            }

            int passwordCallback(char* buffer, int size, int writing, void* user)
            {
                (void)writing;
                if (user == nullptr || size <= 0)
                {
                    return 0;
                }
                const char* password = static_cast<const char*>(user);
                size_t length = std::strlen(password);
                size_t room = static_cast<size_t>(size);
                if (length > room)
                {
                    length = room;
                }
                std::memcpy(buffer, password, length);
                return static_cast<int>(length);
            }

            KiottyNetCode loadIdentity(SSL_CTX* context, const KiottyNetTlsIdentity& identity)
            {
                if (!terminated(identity.cert_file) || !terminated(identity.key_file) || !terminated(identity.key_password))
                {
                    return KIOTTY_INVALID_ARGUMENT;
                }
                if (!fileExists(identity.cert_file) || !fileExists(identity.key_file))
                {
                    return KIOTTY_TLS_CERTIFICATE_NOT_FOUND;
                }
                void* password = const_cast<char*>(identity.key_password);
                SSL_CTX_set_default_passwd_cb(context, passwordCallback);
                SSL_CTX_set_default_passwd_cb_userdata(context, password);
                int certified = SSL_CTX_use_certificate_chain_file(context, identity.cert_file);
                int keyed = 0;
                if (certified == 1)
                {
                    keyed = SSL_CTX_use_PrivateKey_file(context, identity.key_file, SSL_FILETYPE_PEM);
                }
                SSL_CTX_set_default_passwd_cb_userdata(context, nullptr);
                if (keyed != 1)
                {
                    return KIOTTY_TLS_CERTIFICATE_INVALID;
                }
                if (SSL_CTX_check_private_key(context) != 1)
                {
                    return KIOTTY_TLS_CERTIFICATE_INVALID;
                }
                return KIOTTY_SUCCESS;
            }

            KiottyNetCode loadTrustStore(SSL_CTX* context, const char (&path)[KIOTTY_MAX_PATH_LENGTH])
            {
                if (!terminated(path))
                {
                    return KIOTTY_INVALID_ARGUMENT;
                }
                if (!pathExists(path))
                {
                    return KIOTTY_TLS_CERTIFICATE_NOT_FOUND;
                }
                if (SSL_CTX_load_verify_file(context, path) == 1)
                {
                    return KIOTTY_SUCCESS;
                }
                if (SSL_CTX_load_verify_dir(context, path) == 1)
                {
                    ERR_clear_error();
                    return KIOTTY_SUCCESS;
                }
                return KIOTTY_TLS_CERTIFICATE_INVALID;
            }

            void loadDefaultTrustStore(SSL_CTX* context)
            {
                SSL_CTX_set_default_verify_paths(context);
#if defined(_WIN32)
                SSL_CTX_load_verify_store(context, "org.openssl.winstore://");
#endif
                ERR_clear_error();
            }

            bool ipLiteral(const char* hostname)
            {
                bool digits_and_dots = true;
                for (const char* c = hostname; *c != '\0'; ++c)
                {
                    if (*c == ':')
                    {
                        return true;
                    }
                    bool digit = *c >= '0' && *c <= '9';
                    if (!digit && *c != '.')
                    {
                        digits_and_dots = false;
                    }
                }
                return digits_and_dots;
            }

            int acceptAnyPeer(int verified, X509_STORE_CTX* store)
            {
                (void)verified;
                (void)store;
                return 1;
            }

            struct WarmIdentity
            {
                EVP_PKEY* key;
                X509*     certificate;
            };

            X509* selfSigned(EVP_PKEY* key)
            {
                X509* certificate = X509_new();
                if (certificate == nullptr)
                {
                    return nullptr;
                }
                X509_set_version(certificate, 2);
                ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
                X509_gmtime_adj(X509_getm_notBefore(certificate), 0);
                X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
                X509_set_pubkey(certificate, key);
                X509_NAME* name = X509_get_subject_name(certificate);
                const unsigned char* common_name = reinterpret_cast<const unsigned char*>("localhost");
                X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, common_name, -1, -1, 0);
                X509_set_issuer_name(certificate, name);
                if (X509_sign(certificate, key, EVP_sha256()) == 0)
                {
                    X509_free(certificate);
                    return nullptr;
                }
                return certificate;
            }

            WarmIdentity throughPem(EVP_PKEY* key, X509* certificate)
            {
                WarmIdentity identity = { nullptr, nullptr };
                BIO* pem = BIO_new(BIO_s_mem());
                if (pem == nullptr)
                {
                    return identity;
                }
                void* password = const_cast<char*>(WARM_PASSWORD);
                PEM_write_bio_X509(pem, certificate);
                PEM_write_bio_PrivateKey(pem, key, nullptr, nullptr, 0, nullptr, nullptr);
                PEM_write_bio_PrivateKey(pem, key, EVP_aes_256_cbc(), nullptr, 0, nullptr, password);
                identity.certificate = PEM_read_bio_X509_AUX(pem, nullptr, nullptr, nullptr);
                EVP_PKEY* plain = PEM_read_bio_PrivateKey(pem, nullptr, nullptr, nullptr);
                EVP_PKEY_free(plain);
                identity.key = PEM_read_bio_PrivateKey(pem, nullptr, nullptr, password);
                BIO_free(pem);
                return identity;
            }

            SSL_CTX* warmContext(const SSL_METHOD* method, const WarmIdentity& identity)
            {
                SSL_CTX* context = SSL_CTX_new(method);
                if (context == nullptr)
                {
                    return nullptr;
                }
                configureProtocol(context, KIOTTY_TLS_V12 | KIOTTY_TLS_V13, 0, 0);
                SSL_CTX_set_security_level(context, 0);
                SSL_CTX_use_certificate(context, identity.certificate);
                SSL_CTX_use_PrivateKey(context, identity.key);
                SSL_CTX_check_private_key(context);
                X509_STORE_add_cert(SSL_CTX_get_cert_store(context), identity.certificate);
                SSL_CTX_set_verify(context, SSL_VERIFY_PEER, acceptAnyPeer);
                return context;
            }

            void warmHandshake(SSL_CTX* server_context, SSL_CTX* client_context, int version)
            {
                SSL* server = SSL_new(server_context);
                SSL* client = SSL_new(client_context);
                BIO* server_side = nullptr;
                BIO* client_side = nullptr;
                int paired = 0;
                if (server != nullptr && client != nullptr)
                {
                    paired = BIO_new_bio_pair(&server_side, BIO_PAIR_BYTES, &client_side, BIO_PAIR_BYTES);
                }
                if (paired == 1)
                {
                    SSL_set_bio(server, server_side, server_side);
                    SSL_set_bio(client, client_side, client_side);
                    SSL_set_min_proto_version(client, version);
                    SSL_set_max_proto_version(client, version);
                    SSL_set_tlsext_host_name(client, "localhost");
                    SSL_set1_host(client, "localhost");
                    SSL_set_accept_state(server);
                    SSL_set_connect_state(client);
                    bool server_done = false;
                    bool client_done = false;
                    for (int round = 0; round < WARM_ROUNDS && !(server_done && client_done); ++round)
                    {
                        client_done = client_done || SSL_do_handshake(client) == 1;
                        server_done = server_done || SSL_do_handshake(server) == 1;
                    }
                    char byte = 'w';
                    size_t moved = 0;
                    SSL_write_ex(client, &byte, 1, &moved);
                    SSL_read_ex(server, &byte, 1, &moved);
                    SSL_write_ex(server, &byte, 1, &moved);
                    SSL_read_ex(client, &byte, 1, &moved);
                    SSL_shutdown(client);
                    SSL_shutdown(server);
                }
                SSL_free(client);
                SSL_free(server);
                ERR_clear_error();
            }

            void warmWith(EVP_PKEY* key)
            {
                if (key == nullptr)
                {
                    return;
                }
                X509* certificate = selfSigned(key);
                WarmIdentity identity = { nullptr, nullptr };
                if (certificate != nullptr)
                {
                    identity = throughPem(key, certificate);
                }
                if (identity.key != nullptr && identity.certificate != nullptr)
                {
                    SSL_CTX* server = warmContext(TLS_server_method(), identity);
                    SSL_CTX* client = warmContext(TLS_client_method(), identity);
                    if (server != nullptr && client != nullptr)
                    {
                        warmHandshake(server, client, TLS1_3_VERSION);
                        warmHandshake(server, client, TLS1_2_VERSION);
                    }
                    SSL_CTX_free(client);
                    SSL_CTX_free(server);
                }
                EVP_PKEY_free(identity.key);
                X509_free(identity.certificate);
                X509_free(certificate);
                EVP_PKEY_free(key);
                ERR_clear_error();
            }

        }

        bool tlsSupported()
        {
            return true;
        }

        bool installTlsMemory(const TlsMemoryFunctions& functions)
        {
#if KIOTTY_NET_USES_TLS_STATIC_LIBRARY
            CRYPTO_malloc_fn allocate = nullptr;
            CRYPTO_realloc_fn reallocate = nullptr;
            CRYPTO_free_fn release = nullptr;
            CRYPTO_get_mem_functions(&allocate, &reallocate, &release);
            bool hooked = allocate == functions.allocate;
            if (!hooked && allocate == CRYPTO_malloc)
            {
                hooked = CRYPTO_set_mem_functions(functions.allocate, functions.reallocate, functions.release) == 1;
            }
#else
            (void)functions;
            bool hooked = false;
#endif
            OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT, nullptr);
            return hooked;
        }

        void warmTlsMemory()
        {
            warmWith(EVP_RSA_gen(WARM_RSA_BITS));
            warmWith(EVP_EC_gen("P-256"));
        }

        void attachTlsThread()
        {
            unsigned char bytes[1];
            ERR_clear_error();
            RAND_bytes(bytes, sizeof(bytes));
            RAND_priv_bytes(bytes, sizeof(bytes));
            ERR_clear_error();
        }

        void detachTlsThread()
        {
            OPENSSL_thread_stop();
        }

        bool tlsThreadStateFile(const char* file)
        {
            if (file == nullptr)
            {
                return false;
            }
            bool slash = std::strstr(file, "crypto/err/") != nullptr;
            bool backslash = std::strstr(file, "crypto\\err\\") != nullptr;
            return slash || backslash;
        }

        TlsProfile::TlsProfile()
            : _context(nullptr)
            , _server(false)
            , _verify_hostname(false)
        {
        }

        TlsProfile::~TlsProfile()
        {
            close();
        }

        KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfig& config)
        {
            close();
            bool authenticates = config.client_auth != KIOTTY_CLIENT_AUTH_NONE;
            SSL_CTX* context = SSL_CTX_new(TLS_server_method());
            if (context == nullptr)
            {
                ERR_clear_error();
                return KIOTTY_TLS_NOT_SUPPORTED;
            }
            KiottyNetCode code = configureProtocol(context, config.versions, config.v12_cipher_suites, config.v13_cipher_suites);
            if (code == KIOTTY_SUCCESS)
            {
                code = loadIdentity(context, config.identity);
            }
            if (code == KIOTTY_SUCCESS && authenticates)
            {
                code = loadTrustStore(context, config.trust_store_path);
            }
            if (code != KIOTTY_SUCCESS)
            {
                ERR_clear_error();
                SSL_CTX_free(context);
                return code;
            }
            int mode = SSL_VERIFY_NONE;
            if (config.client_auth == KIOTTY_CLIENT_AUTH_OPTIONAL)
            {
                mode = SSL_VERIFY_PEER;
            }
            if (config.client_auth == KIOTTY_CLIENT_AUTH_REQUIRED)
            {
                mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            }
            SSL_CTX_set_verify(context, mode, nullptr);
            _context = context;
            _server = true;
            _verify_hostname = false;
            return KIOTTY_SUCCESS;
        }

        KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfig& config)
        {
            close();
            bool verify_server = config.skip_server_verification == 0;
            SSL_CTX* context = SSL_CTX_new(TLS_client_method());
            if (context == nullptr)
            {
                ERR_clear_error();
                return KIOTTY_TLS_NOT_SUPPORTED;
            }
            KiottyNetCode code = configureProtocol(context, config.versions, config.v12_cipher_suites, config.v13_cipher_suites);
            if (code == KIOTTY_SUCCESS && config.trust_store_path[0] != '\0')
            {
                code = loadTrustStore(context, config.trust_store_path);
            }
            if (code == KIOTTY_SUCCESS && config.trust_store_path[0] == '\0')
            {
                loadDefaultTrustStore(context);
            }
            if (code == KIOTTY_SUCCESS && config.identity.cert_file[0] != '\0')
            {
                code = loadIdentity(context, config.identity);
            }
            if (code != KIOTTY_SUCCESS)
            {
                ERR_clear_error();
                SSL_CTX_free(context);
                return code;
            }
            int mode = verify_server ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
            SSL_CTX_set_verify(context, mode, nullptr);
            _context = context;
            _server = false;
            _verify_hostname = verify_server && config.skip_hostname_verification == 0;
            return KIOTTY_SUCCESS;
        }

        void TlsProfile::close()
        {
            SSL_CTX_free(_context);
            _context = nullptr;
        }

        bool TlsProfile::valid() const
        {
            return _context != nullptr;
        }

        bool TlsProfile::server() const
        {
            return _server;
        }

        bool TlsProfile::verifyHostname() const
        {
            return _verify_hostname;
        }

        uint32_t TlsProfile::handshakeTimeoutMs() const
        {
            return TLS_HANDSHAKE_TIMEOUT_MS;
        }

        ssl_ctx_st* TlsProfile::context() const
        {
            return _context;
        }

        TlsEngine::~TlsEngine()
        {
            SSL_free(_ssl);
            BIO_free(_network);
        }

        KiottyNetCode TlsEngine::open(TlsProfile& profile, const char* hostname)
        {
            bool sized = _cipher_receive.capacity() > 0 && _cipher_send.capacity() > 0;
            if (!sized || !profile.valid())
            {
                return KIOTTY_INVALID_ARGUMENT;
            }
            close();
            if (_ssl != nullptr && SSL_get_SSL_CTX(_ssl) != profile.context())
            {
                SSL_free(_ssl);
                _ssl = nullptr;
            }
            if (_ssl != nullptr && SSL_clear(_ssl) != 1)
            {
                SSL_free(_ssl);
                _ssl = nullptr;
            }
            if (_ssl == nullptr)
            {
                BIO_free(_network);
                _network = nullptr;
                _ssl = SSL_new(profile.context());
                BIO* internal = nullptr;
                int paired = BIO_new_bio_pair(&internal, BIO_PAIR_BYTES, &_network, BIO_PAIR_BYTES);
                if (_ssl == nullptr || paired != 1)
                {
                    BIO_free(internal);
                    BIO_free(_network);
                    _network = nullptr;
                    SSL_free(_ssl);
                    _ssl = nullptr;
                    ERR_clear_error();
                    return KIOTTY_TLS_NOT_SUPPORTED;
                }
                SSL_set_bio(_ssl, internal, internal);
            }
            BIO* internal = SSL_get_rbio(_ssl);
            BIO_reset(internal);
            BIO_reset(_network);

            if (profile.server())
            {
                SSL_set_accept_state(_ssl);
            }
            else
            {
                SSL_set_connect_state(_ssl);
            }
            bool named = hostname != nullptr && hostname[0] != '\0';
            if (named && !ipLiteral(hostname))
            {
                SSL_set_tlsext_host_name(_ssl, hostname);
            }
            SSL_set1_host(_ssl, nullptr);
            if (named && profile.verifyHostname())
            {
                SSL_set1_host(_ssl, hostname);
            }

            _active = true;
            _failed = false;
            _last_code = KIOTTY_SUCCESS;
            _last_message = "";
            return KIOTTY_SUCCESS;
        }

        void TlsEngine::close()
        {
            _active = false;
            _cipher_receive.clear();
            _cipher_send.clear();
        }

        TlsStep TlsEngine::handshake()
        {
            if (!_active)
            {
                return fail(KIOTTY_INVALID_ARGUMENT, "tls: engine is not open");
            }
            if (_failed)
            {
                return TlsStep::FAILED;
            }
            int result = 0;
            int reason = SSL_ERROR_NONE;
            bool progressing = true;
            while (progressing)
            {
                size_t fed = feed();
                ERR_clear_error();
                result = SSL_do_handshake(_ssl);
                reason = SSL_get_error(_ssl, result);
                size_t flushed = flush();
                bool retry = result != 1 && (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE);
                progressing = retry && (fed > 0 || flushed > 0);
            }
            if (result != 1 && reason == SSL_ERROR_ZERO_RETURN)
            {
                return fail(KIOTTY_PEER_CLOSED, "tls: peer closed during handshake");
            }
            bool retry = reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE;
            if (result != 1 && !retry)
            {
                unsigned long error_code = ERR_peek_last_error();
                int library_reason = ERR_GET_REASON(error_code);
                if (library_reason == SSL_R_CERTIFICATE_VERIFY_FAILED)
                {
                    long verify = SSL_get_verify_result(_ssl);
                    const char* message = X509_verify_cert_error_string(verify);
                    bool hostname = verify == X509_V_ERR_HOSTNAME_MISMATCH || verify == X509_V_ERR_IP_ADDRESS_MISMATCH;
                    return fail(hostname ? KIOTTY_TLS_HOSTNAME_MISMATCH : KIOTTY_TLS_VERIFY_FAILED, message);
                }
                if (library_reason == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE)
                {
                    return fail(KIOTTY_TLS_VERIFY_FAILED, lastErrorMessage());
                }
                return fail(KIOTTY_TLS_HANDSHAKE_FAILED, lastErrorMessage());
            }
            StageBase::Consumer waiting = _cipher_send.consumer();
            if (waiting.readable() > 0)
            {
                return TlsStep::SEND;
            }
            if (result == 1)
            {
                return TlsStep::DONE;
            }
            return TlsStep::RECEIVE;
        }

        DecodeResult TlsEngine::decode(StageBase& plain)
        {
            if (!_active || _failed)
            {
                _last_code = KIOTTY_INVALID_ARGUMENT;
                _last_message = "tls: engine is not open";
                return DecodeResult::failure(KIOTTY_INVALID_ARGUMENT);
            }
            feed();
            size_t total = 0;
            KiottyNetCode code = KIOTTY_SUCCESS;
            StageBase::Provider provider = plain.provider();
            while (provider.writable() > 0)
            {
                size_t room = provider.writable();
                char* target = provider.prepare(room);
                size_t read = 0;
                ERR_clear_error();
                int result = SSL_read_ex(_ssl, target, room, &read);
                if (result == 1)
                {
                    provider.commit(read);
                    total += read;
                    continue;
                }
                int reason = SSL_get_error(_ssl, result);
                if (reason == SSL_ERROR_WANT_READ)
                {
                    size_t fed = feed();
                    if (fed > 0)
                    {
                        continue;
                    }
                    break;
                }
                if (reason == SSL_ERROR_WANT_WRITE)
                {
                    size_t flushed = flush();
                    if (flushed > 0)
                    {
                        continue;
                    }
                    break;
                }
                if (reason == SSL_ERROR_ZERO_RETURN)
                {
                    code = KIOTTY_PEER_CLOSED;
                    _last_message = "tls: close_notify from peer";
                }
                else
                {
                    code = KIOTTY_TLS_RECORD_ERROR;
                    _last_message = lastErrorMessage();
                }
                break;
            }
            int update = SSL_get_key_update_type(_ssl);
            if (update != SSL_KEY_UPDATE_NONE)
            {
                SSL_key_update(_ssl, SSL_KEY_UPDATE_NOT_REQUESTED);
                SSL_do_handshake(_ssl);
            }
            flush();
            if (code != KIOTTY_SUCCESS && total == 0)
            {
                _last_code = code;
                return DecodeResult::failure(code);
            }
            return DecodeResult::success(total);
        }

        void TlsEngine::encode(KiottyNetSpan plain)
        {
            if (plain.length == 0)
            {
                return;
            }
            assert(_active && !_failed);
            if (!_active || _failed)
            {
                return;
            }
            StageBase::Consumer waiting = _cipher_send.consumer();
            if (waiting.readable() == 0)
            {
                _cipher_send.compact();
            }
            StageBase::Provider provider = _cipher_send.provider();
            size_t bound = tlsCipherBound(plain.length);
            size_t need = pending() + bound;
            assert(need <= provider.writable());
            if (need > provider.writable())
            {
                return;
            }
            size_t written = 0;
            int result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
            int reason = SSL_get_error(_ssl, result);
            while (result != 1 && reason == SSL_ERROR_WANT_WRITE)
            {
                size_t flushed = flush();
                if (flushed == 0)
                {
                    break;
                }
                result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
                reason = SSL_get_error(_ssl, result);
            }
            assert(result == 1 && written == plain.length);
            if (result != 1)
            {
                ERR_clear_error();
            }
            flush();
        }

#if defined(KIOTTY_NET_TESTING)
        void TlsEngine::debugKeyUpdate()
        {
            SSL_key_update(_ssl, SSL_KEY_UPDATE_REQUESTED);
            SSL_do_handshake(_ssl);
            flush();
        }

        void TlsEngine::debugShutdown()
        {
            SSL_shutdown(_ssl);
            flush();
        }
#endif

        size_t TlsEngine::feed()
        {
            StageBase::Consumer consumer = _cipher_receive.consumer();
            size_t readable = consumer.readable();
            size_t room = BIO_ctrl_get_write_guarantee(_network);
            size_t length = readable < room ? readable : room;
            size_t written = 0;
            if (length > 0)
            {
                BIO_write_ex(_network, consumer.peek(), length, &written);
                consumer.commit(written);
            }
            _cipher_receive.compact();
            return written;
        }

        size_t TlsEngine::flush()
        {
            StageBase::Consumer waiting = _cipher_send.consumer();
            if (waiting.readable() == 0)
            {
                _cipher_send.compact();
            }
            StageBase::Provider provider = _cipher_send.provider();
            size_t room = provider.writable();
            size_t queued = pending();
            size_t length = room < queued ? room : queued;
            if (length == 0)
            {
                return 0;
            }
            char* target = provider.prepare(length);
            size_t read = 0;
            BIO_read_ex(_network, target, length, &read);
            provider.commit(read);
            return read;
        }

        size_t TlsEngine::pending() const
        {
            return BIO_ctrl_pending(_network);
        }
    }
}

#endif
