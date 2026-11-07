#if KIOTTY_NET_USE_TLS

#include "data/datasource/kiotty_memory_layout.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/infra/tls/kiotty_tls_pipe.h"
#include "data/infra/tls/kiotty_tls_profile.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <sys/stat.h>

#include <cassert>
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
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_AES256_GCM_SHA384, "ECDHE-ECDSA-AES256-GCM-SHA384" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_AES256_GCM_SHA384, "ECDHE-RSA-AES256-GCM-SHA384" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_CHACHA20_POLY1305, "ECDHE-ECDSA-CHACHA20-POLY1305" },
            { KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_CHACHA20_POLY1305, "ECDHE-RSA-CHACHA20-POLY1305" }
        };

        const SuiteName V13_SUITES[] =
        {
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_GCM_SHA256, "TLS_AES_128_GCM_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_256_GCM_SHA384, "TLS_AES_256_GCM_SHA384" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_CHACHA20_POLY1305_SHA256, "TLS_CHACHA20_POLY1305_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM_SHA256, "TLS_AES_128_CCM_SHA256" },
            { KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM8_SHA256, "TLS_AES_128_CCM_8_SHA256" }
        };

        const size_t SUITE_LIST_BYTES = 256;
        const size_t SEPARATOR_AND_TERMINATOR_BYTES = 2;
        const size_t BIO_PAIR_BYTES = 4096;
        const long   MAX_CERT_LIST_BYTES = 16 * 1024;

        const char* const NOT_OPEN_MESSAGE = "tls: engine is not open";
        const char* const HANDSHAKE_CLOSED_MESSAGE = "tls: peer closed during handshake";
        const char* const CLOSE_NOTIFY_MESSAGE = "tls: close_notify from peer";

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
            const unsigned long error_code = ERR_peek_last_error();
            const char* reason = ERR_reason_error_string(error_code);
            if (reason == nullptr) { return "tls: unknown error"; }
            return reason;
        }

        template <size_t N>
        bool buildSuiteList(Span<char> list, uint32_t bits, const SuiteName (&suites)[N])
        {
            size_t used = 0;
            for (size_t i = 0; i < N; ++i)
            {
                if ((bits & suites[i].bit) == 0) { continue; }
                const size_t length = strnlen(suites[i].name, SUITE_LIST_BYTES);
                if (used + length + SEPARATOR_AND_TERMINATOR_BYTES > list.length) { return false; }
                if (used > 0)
                {
                    list[used] = ':';
                    ++used;
                }
                const Span<char> target = list.skip(used);
                const bool copied = target.copyFrom(Span<const char>{ suites[i].name, length });
                if (!copied) { return false; }
                used += length;
            }
            list[used] = '\0';
            return true;
        }

        KiottyNetCode applyV12Suites(SSL_CTX* context, uint32_t bits)
        {
            if (bits == 0) { return KIOTTY_NET_CODE_SUCCESS; }
            char list[SUITE_LIST_BYTES]{};
            const bool built = buildSuiteList(Span<char>{ list, SUITE_LIST_BYTES }, bits, V12_SUITES);
            if (!built) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const int applied = SSL_CTX_set_cipher_list(context, list);
            if (applied != 1) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode applyV13Suites(SSL_CTX* context, uint32_t bits)
        {
            if (bits == 0) { return KIOTTY_NET_CODE_SUCCESS; }
            char list[SUITE_LIST_BYTES]{};
            const bool built = buildSuiteList(Span<char>{ list, SUITE_LIST_BYTES }, bits, V13_SUITES);
            if (!built) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const int applied = SSL_CTX_set_ciphersuites(context, list);
            if (applied != 1) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        void restrictFeatures(SSL_CTX* context)
        {
            SSL_CTX_set_options(context, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
            SSL_CTX_set_mode(context, SSL_MODE_RELEASE_BUFFERS);
            SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);
            SSL_CTX_set_num_tickets(context, 0);
            SSL_CTX_set_max_cert_list(context, MAX_CERT_LIST_BYTES);
        }

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

        bool identityTerminated(const KiottyNetTlsIdentity& identity)
        {
            const bool certificate = terminated(identity.cert_file);
            const bool key = terminated(identity.key_file);
            const bool password = terminated(identity.key_password);
            return certificate && key && password;
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

        KiottyNetCode loadTrustStore(SSL_CTX* context, const char (&path)[KIOTTY_NET_MAX_PATH_LENGTH])
        {
            const bool is_terminated = terminated(path);
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

        KiottyNetCode loadClientTrust(SSL_CTX* context, const char (&path)[KIOTTY_NET_MAX_PATH_LENGTH])
        {
            const bool has_path = path[0] != '\0';
            if (has_path)
            {
                return loadTrustStore(context, path);
            }
            loadDefaultTrustStore(context);
            return KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetCode prepareServerContext(SSL_CTX* context, const KiottyNetTlsServerConfiguration& config)
        {
            const ProtocolSelection protocol{
                config.versions, config.v12_cipher_suites, config.v13_cipher_suites };
            const KiottyNetCode configured = configureProtocol(context, protocol);
            if (configured != KIOTTY_NET_CODE_SUCCESS) { return configured; }
            const KiottyNetCode loaded = loadIdentity(context, config.identity);
            if (loaded != KIOTTY_NET_CODE_SUCCESS) { return loaded; }
            const bool authenticates = config.client_authentication != KIOTTY_NET_CLIENT_AUTHENTICATION_NONE;
            if (!authenticates) { return KIOTTY_NET_CODE_SUCCESS; }
            return loadTrustStore(context, config.trust_store_path);
        }

        KiottyNetCode prepareClientContext(SSL_CTX* context, const KiottyNetTlsClientConfiguration& config)
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

        KiottyNetCode abandonContext(SSL_CTX* context, KiottyNetCode code)
        {
            ERR_clear_error();
            SSL_CTX_free(context);
            return code;
        }

        bool ipLiteral(const char* hostname)
        {
            bool digits_and_dots = true;
            for (const char* c = hostname; *c != '\0'; ++c)
            {
                if (*c == ':') { return true; }
                const bool digit = *c >= '0' && *c <= '9';
                if (!digit && *c != '.')
                {
                    digits_and_dots = false;
                }
            }
            return digits_and_dots;
        }

        bool isNamed(const char* hostname)
        {
            return hostname != nullptr && hostname[0] != '\0';
        }

        bool isServerName(const char* hostname)
        {
            const bool is_named = isNamed(hostname);
            if (!is_named) { return false; }
            const bool is_literal = ipLiteral(hostname);
            return !is_literal;
        }

        SSL* reuseSsl(SSL* ssl, SSL_CTX* context)
        {
            if (ssl == nullptr) { return nullptr; }
            const SSL_CTX* owner = SSL_get_SSL_CTX(ssl);
            if (owner != context)
            {
                SSL_free(ssl);
                return nullptr;
            }
            const int cleared = SSL_clear(ssl);
            if (cleared != 1)
            {
                SSL_free(ssl);
                return nullptr;
            }
            return ssl;
        }

        void startRole(SSL* ssl, const TlsProfile& profile)
        {
            const bool is_server = profile.server();
            if (is_server)
            {
                SSL_set_accept_state(ssl);
                return;
            }
            SSL_set_connect_state(ssl);
        }

        void setHostname(SSL* ssl, const TlsProfile& profile, const char* hostname)
        {
            const bool is_server_name = isServerName(hostname);
            if (is_server_name)
            {
                SSL_set_tlsext_host_name(ssl, hostname);
            }
            SSL_set1_host(ssl, nullptr);
            const bool is_named = isNamed(hostname);
            const bool verifies = is_named && profile.verifyHostname();
            if (verifies)
            {
                SSL_set1_host(ssl, hostname);
            }
        }

        bool isRetryable(int reason)
        {
            return reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE;
        }

        TlsFailure verificationFailure(const SSL* ssl)
        {
            const long verify = SSL_get_verify_result(ssl);
            const char* message = X509_verify_cert_error_string(verify);
            const bool is_hostname = verify == X509_V_ERR_HOSTNAME_MISMATCH;
            const bool is_address = verify == X509_V_ERR_IP_ADDRESS_MISMATCH;
            if (is_hostname || is_address)
            {
                return TlsFailure{ message, KIOTTY_NET_CODE_TLS_HOSTNAME_MISMATCH };
            }
            return TlsFailure{ message, KIOTTY_NET_CODE_TLS_VERIFY_FAILED };
        }

        TlsFailure handshakeFailure(const SSL* ssl)
        {
            const unsigned long error_code = ERR_peek_last_error();
            const int library_reason = ERR_GET_REASON(error_code);
            if (library_reason == SSL_R_CERTIFICATE_VERIFY_FAILED) { return verificationFailure(ssl); }
            const char* message = lastErrorMessage();
            if (library_reason == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE)
            {
                return TlsFailure{ message, KIOTTY_NET_CODE_TLS_VERIFY_FAILED };
            }
            return TlsFailure{ message, KIOTTY_NET_CODE_TLS_HANDSHAKE_FAILED };
        }

        TlsFailure readFailure(int reason)
        {
            if (reason == SSL_ERROR_ZERO_RETURN)
            {
                return TlsFailure{ CLOSE_NOTIFY_MESSAGE, KIOTTY_NET_CODE_PEER_CLOSED };
            }
            const char* message = lastErrorMessage();
            return TlsFailure{ message, KIOTTY_NET_CODE_TLS_RECORD_ERROR };
        }

        void answerKeyUpdate(SSL* ssl)
        {
            const int update = SSL_get_key_update_type(ssl);
            if (update == SSL_KEY_UPDATE_NONE) { return; }
            SSL_key_update(ssl, SSL_KEY_UPDATE_NOT_REQUESTED);
            SSL_do_handshake(ssl);
        }
    }

    bool tlsSupported()
    {
        return true;
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

    KiottyNetCode TlsProfile::openServer(const KiottyNetTlsServerConfiguration& config)
    {
        close();
        SSL_CTX* context = SSL_CTX_new(TLS_server_method());
        if (context == nullptr) { return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED); }
        const KiottyNetCode prepared = prepareServerContext(context, config);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = clientVerifyMode(config.client_authentication);
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _server = true;
        _verify_hostname = false;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode TlsProfile::openClient(const KiottyNetTlsClientConfiguration& config)
    {
        close();
        const bool verify_server = config.skip_server_verification == 0;
        SSL_CTX* context = SSL_CTX_new(TLS_client_method());
        if (context == nullptr) { return abandonContext(context, KIOTTY_NET_CODE_TLS_NOT_SUPPORTED); }
        const KiottyNetCode prepared = prepareClientContext(context, config);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return abandonContext(context, prepared); }
        const int mode = verify_server ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
        SSL_CTX_set_verify(context, mode, nullptr);
        _context = context;
        _server = false;
        _verify_hostname = verify_server && config.skip_hostname_verification == 0;
        return KIOTTY_NET_CODE_SUCCESS;
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

    uint32_t TlsProfile::handshakeTimeoutMilliseconds() const
    {
        return TLS_HANDSHAKE_TIMEOUT_MILLISECONDS;
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
        const bool has_capacity = _cipher_receive.capacity() > 0 && _cipher_send.capacity() > 0;
        if (!has_capacity) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_valid = profile.valid();
        if (!is_valid) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        close();
        SSL_CTX* context = profile.context();
        _ssl = reuseSsl(_ssl, context);
        if (_ssl == nullptr)
        {
            const KiottyNetCode created = createSsl(context);
            if (created != KIOTTY_NET_CODE_SUCCESS) { return created; }
        }
        BIO* internal = SSL_get_rbio(_ssl);
        BIO_reset(internal);
        BIO_reset(_network);
        startRole(_ssl, profile);
        setHostname(_ssl, profile, hostname);
        _active = true;
        _failed = false;
        _failure = TlsFailure{ "", KIOTTY_NET_CODE_SUCCESS };
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void TlsEngine::close()
    {
        _active = false;
        _cipher_receive.clear();
        _cipher_send.clear();
    }

    TlsStep TlsEngine::handshake()
    {
        if (!_active) { return fail(KIOTTY_NET_CODE_INVALID_ARGUMENT, NOT_OPEN_MESSAGE); }
        if (_failed) { return TlsStep::FAILED; }
        const Outcome outcome = driveHandshake();
        const bool is_complete = outcome.result == 1;
        const bool is_closed = outcome.reason == SSL_ERROR_ZERO_RETURN;
        if (!is_complete && is_closed)
        {
            return fail(KIOTTY_NET_CODE_PEER_CLOSED, HANDSHAKE_CLOSED_MESSAGE);
        }
        const bool is_retryable = isRetryable(outcome.reason);
        if (!is_complete && !is_retryable)
        {
            const TlsFailure rejected = handshakeFailure(_ssl);
            return fail(rejected.code, rejected.message);
        }
        StageBase::Consumer waiting = _cipher_send.consumer();
        const size_t readable = waiting.readable();
        if (readable > 0) { return TlsStep::SEND; }
        if (is_complete) { return TlsStep::DONE; }
        return TlsStep::RECEIVE;
    }

    DecodeResult TlsEngine::decode(StageBase& plain)
    {
        StageBase::Provider provider = plain.provider();
        const size_t room = provider.writable();
        char* target = provider.prepare(room);
        const DecodeResult decoded = decode(target, room);
        if (decoded)
        {
            provider.commit(decoded.value);
        }
        return decoded;
    }

    DecodeResult TlsEngine::decode(char* buffer, size_t length)
    {
        if (!_active || _failed)
        {
            _failure = TlsFailure{ NOT_OPEN_MESSAGE, KIOTTY_NET_CODE_INVALID_ARGUMENT };
            return DecodeResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }
        feed();
        const Drained drained = drain(Span<char>{ buffer, length });
        answerKeyUpdate(_ssl);
        flush();
        if (drained.code != KIOTTY_NET_CODE_SUCCESS && drained.length == 0)
        {
            _failure.code = drained.code;
            return DecodeResult::failure(drained.code);
        }
        return DecodeResult::success(drained.length);
    }

    void TlsEngine::encode(KiottyNetSpan plain)
    {
        if (plain.length == 0) { return; }
        assert(_active && !_failed);
        if (!_active || _failed) { return; }
        const bool fits = fitsCipherSend(plain.length);
        assert(fits);
        if (!fits) { return; }
        writeRecord(plain);
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

    KiottyNetCode TlsEngine::createSsl(ssl_ctx_st* context)
    {
        BIO_free(_network);
        _network = nullptr;
        _ssl = SSL_new(context);
        BIO* internal = nullptr;
        const int paired = BIO_new_bio_pair(&internal, BIO_PAIR_BYTES, &_network, BIO_PAIR_BYTES);
        if (_ssl == nullptr || paired != 1)
        {
            BIO_free(internal);
            BIO_free(_network);
            _network = nullptr;
            SSL_free(_ssl);
            _ssl = nullptr;
            ERR_clear_error();
            return KIOTTY_NET_CODE_TLS_NOT_SUPPORTED;
        }
        SSL_set_bio(_ssl, internal, internal);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    TlsEngine::Outcome TlsEngine::driveHandshake()
    {
        Outcome outcome{ 0, SSL_ERROR_NONE };
        bool progressing = true;
        while (progressing)
        {
            const size_t fed = feed();
            ERR_clear_error();
            outcome.result = SSL_do_handshake(_ssl);
            outcome.reason = SSL_get_error(_ssl, outcome.result);
            const size_t flushed = flush();
            const bool retry = outcome.result != 1 && isRetryable(outcome.reason);
            progressing = retry && (fed > 0 || flushed > 0);
        }
        return outcome;
    }

    TlsEngine::Drained TlsEngine::drain(Span<char> target)
    {
        Drained drained{ 0, KIOTTY_NET_CODE_SUCCESS };
        while (drained.length < target.length)
        {
            const Span<char> rest = target.skip(drained.length);
            size_t read = 0;
            ERR_clear_error();
            const int result = SSL_read_ex(_ssl, rest.data, rest.length, &read);
            if (result == 1)
            {
                drained.length += read;
                continue;
            }
            const int reason = SSL_get_error(_ssl, result);
            const size_t resumed = resume(reason);
            if (resumed > 0) { continue; }
            const bool is_retryable = isRetryable(reason);
            if (is_retryable) { return drained; }
            const TlsFailure stopped = readFailure(reason);
            _failure.message = stopped.message;
            drained.code = stopped.code;
            return drained;
        }
        return drained;
    }

    size_t TlsEngine::resume(int reason)
    {
        if (reason == SSL_ERROR_WANT_READ) { return feed(); }
        if (reason == SSL_ERROR_WANT_WRITE) { return flush(); }
        return 0;
    }

    bool TlsEngine::fitsCipherSend(size_t plain_length)
    {
        StageBase::Consumer waiting = _cipher_send.consumer();
        const size_t readable = waiting.readable();
        if (readable == 0)
        {
            _cipher_send.compact();
        }
        StageBase::Provider provider = _cipher_send.provider();
        const size_t bound = tlsCipherBound(plain_length);
        const size_t need = pending() + bound;
        return need <= provider.writable();
    }

    void TlsEngine::writeRecord(KiottyNetSpan plain)
    {
        size_t written = 0;
        int result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
        int reason = SSL_get_error(_ssl, result);
        while (result != 1 && reason == SSL_ERROR_WANT_WRITE)
        {
            const size_t flushed = flush();
            if (flushed == 0) { break; }
            result = SSL_write_ex(_ssl, plain.data, plain.length, &written);
            reason = SSL_get_error(_ssl, result);
        }
        assert(result == 1 && written == plain.length);
        if (result != 1)
        {
            ERR_clear_error();
        }
    }

    size_t TlsEngine::feed()
    {
        StageBase::Consumer consumer = _cipher_receive.consumer();
        const size_t readable = consumer.readable();
        const size_t room = BIO_ctrl_get_write_guarantee(_network);
        const size_t length = readable < room ? readable : room;
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
        const size_t readable = waiting.readable();
        if (readable == 0)
        {
            _cipher_send.compact();
        }
        StageBase::Provider provider = _cipher_send.provider();
        const size_t room = provider.writable();
        const size_t queued = pending();
        const size_t length = room < queued ? room : queued;
        if (length == 0) { return 0; }
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

    TlsPipe::TlsPipe(TlsEngine& engine)
        : _engine(engine)
    {
    }

    TlsStep TlsPipe::write(KiottyNetSpan plain)
    {
        if (!_engine._active || _engine._failed)
        {
            return _engine.fail(KIOTTY_NET_CODE_INVALID_ARGUMENT, NOT_OPEN_MESSAGE);
        }
        if (plain.length == 0) { return TlsStep::DONE; }
        size_t written = 0;
        ERR_clear_error();
        const int result = SSL_write_ex(_engine._ssl, plain.data, plain.length, &written);
        if (result == 1) { return TlsStep::DONE; }
        const int reason = SSL_get_error(_engine._ssl, result);
        if (reason == SSL_ERROR_WANT_WRITE) { return TlsStep::SEND; }
        const char* message = lastErrorMessage();
        return _engine.fail(KIOTTY_NET_CODE_TLS_RECORD_ERROR, message);
    }

    Span<char> TlsPipe::receiveRoom()
    {
        const Span<char> none{ nullptr, 0 };
        if (!_engine._active) { return none; }
        _engine.feed();
        StageBase::Consumer waiting = _engine._cipher_receive.consumer();
        const size_t readable = waiting.readable();
        if (readable > 0) { return none; }
        char* target = nullptr;
        const ossl_ssize_t available = BIO_nwrite0(_engine._network, &target);
        if (available <= 0) { return none; }
        return Span<char>{ target, static_cast<size_t>(available) };
    }

    void TlsPipe::received(size_t length)
    {
        if (length == 0) { return; }
        char* target = nullptr;
        BIO_nwrite(_engine._network, &target, static_cast<int>(length));
    }

    Span<const char> TlsPipe::outgoing()
    {
        const Span<const char> none{ nullptr, 0 };
        if (!_engine._active) { return none; }
        char* source = nullptr;
        const ossl_ssize_t available = BIO_nread0(_engine._network, &source);
        if (available <= 0) { return none; }
        return Span<const char>{ source, static_cast<size_t>(available) };
    }

    void TlsPipe::sent(size_t length)
    {
        if (length == 0) { return; }
        char* source = nullptr;
        BIO_nread(_engine._network, &source, static_cast<int>(length));
    }
}

#endif
