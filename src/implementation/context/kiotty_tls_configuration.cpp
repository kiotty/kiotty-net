#include "context/kiotty_tls_configuration.h"

#include "context/kiotty_configuration.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"

#include <cstddef>
#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint32_t VERSION_V12_BIT = static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V12);
        constexpr uint32_t VERSION_V13_BIT = static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V13);
        constexpr uint32_t ALL_VERSIONS = VERSION_V12_BIT | VERSION_V13_BIT;
        constexpr uint32_t ALL_V12_SUITES =
            static_cast<uint32_t>(KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_AES256_GCM_SHA384)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_AES256_GCM_SHA384)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_CHACHA20_POLY1305)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_CHACHA20_POLY1305);
        constexpr uint32_t ALL_V13_SUITES =
            static_cast<uint32_t>(KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_GCM_SHA256)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_256_GCM_SHA384)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V13_CIPHER_SUITE_CHACHA20_POLY1305_SHA256)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM_SHA256)
            | static_cast<uint32_t>(KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM8_SHA256);

        struct IdentityText
        {
            const char* cert_file;
            const char* key_file;
            const char* key_password;
            size_t      cert_length;
            size_t      key_length;
            size_t      password_length;
        };

        static_assert(sizeof(IdentityText) == 3 * sizeof(void*) + 3 * sizeof(size_t),
                      "IdentityText: unexpected padding");

        using IdentityResult = Result<IdentityText, KiottyNetCode>;

        IdentityResult measureIdentity(const char* cert_file,
                                       const char* key_file,
                                       const char* key_password);
        TextLength     measurePassword(const char* key_password);
        TextLength     measureTrust(KiottyNetClientAuthentication mode, const char* path);
        void           writeIdentity(KiottyNetTlsIdentity& identity, const IdentityText& text);
        uint32_t       versionsOf(TlsVersions versions);
        bool           isSuiteSetValid(uint32_t suites, uint32_t known);
    }

    KiottyNetCode initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration& configuration,
                                                   const char* cert_file,
                                                   const char* key_file,
                                                   const char* key_password)
    {
        const IdentityResult identity = measureIdentity(cert_file, key_file, key_password);
        if (!identity) { return identity.code; }
        configuration = KiottyNetTlsServerConfiguration{};
        configuration.initialization_mark = INITIALIZATION_MARK;
        configuration.versions = ALL_VERSIONS;
        configuration.v12_cipher_suites = ALL_V12_SUITES;
        configuration.v13_cipher_suites = ALL_V13_SUITES;
        configuration.client_authentication = KIOTTY_NET_CLIENT_AUTHENTICATION_NONE;
        writeIdentity(configuration.identity, identity.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsServerVersions(KiottyNetTlsServerConfiguration& configuration,
                                             TlsVersions versions)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const uint32_t chosen = versionsOf(versions);
        if (chosen == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.versions = chosen;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsServerV12CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(suites, ALL_V12_SUITES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.v12_cipher_suites = suites;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsServerV13CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(suites, ALL_V13_SUITES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.v13_cipher_suites = suites;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsServerClientAuthentication(
        KiottyNetTlsServerConfiguration& configuration,
        KiottyNetClientAuthentication mode,
        const char* trust_store_path)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const TextLength trust = measureTrust(mode, trust_store_path);
        if (!trust) { return trust.code; }
        configuration.client_authentication = mode;
        const Span<char> target{ configuration.trust_store_path,
                                 sizeof(configuration.trust_store_path) };
        writeText(target, trust_store_path, trust.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration& configuration)
    {
        configuration = KiottyNetTlsClientConfiguration{};
        configuration.initialization_mark = INITIALIZATION_MARK;
        configuration.versions = ALL_VERSIONS;
        configuration.v12_cipher_suites = ALL_V12_SUITES;
        configuration.v13_cipher_suites = ALL_V13_SUITES;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientVersions(KiottyNetTlsClientConfiguration& configuration,
                                             TlsVersions versions)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const uint32_t chosen = versionsOf(versions);
        if (chosen == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.versions = chosen;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(suites, ALL_V12_SUITES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.v12_cipher_suites = suites;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(suites, ALL_V13_SUITES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.v13_cipher_suites = suites;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientTrustStore(KiottyNetTlsClientConfiguration& configuration,
                                               const char* trust_store_path)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const TextLength trust = measureText(trust_store_path, KIOTTY_NET_MAX_PATH_LENGTH);
        if (!trust) { return trust.code; }
        const Span<char> target{ configuration.trust_store_path,
                                 sizeof(configuration.trust_store_path) };
        writeText(target, trust_store_path, trust.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientVerification(KiottyNetTlsClientConfiguration& configuration,
                                                 TlsVerification verification)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_hostname_alone = !verification.verify_server && verification.verify_hostname;
        if (is_hostname_alone) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.skip_server_verification = verification.verify_server ? 0 : 1;
        configuration.skip_hostname_verification = verification.verify_hostname ? 0 : 1;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientIdentity(KiottyNetTlsClientConfiguration& configuration,
                                             const char* cert_file,
                                             const char* key_file,
                                             const char* key_password)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const IdentityResult identity = measureIdentity(cert_file, key_file, key_password);
        if (!identity) { return identity.code; }
        writeIdentity(configuration.identity, identity.value);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isInitialized(const KiottyNetTlsServerConfiguration& configuration)
    {
        return configuration.initialization_mark == INITIALIZATION_MARK;
    }

    bool isInitialized(const KiottyNetTlsClientConfiguration& configuration)
    {
        return configuration.initialization_mark == INITIALIZATION_MARK;
    }

    namespace
    {
        IdentityResult measureIdentity(const char* cert_file,
                                       const char* key_file,
                                       const char* key_password)
        {
            const TextLength cert_length = measureText(cert_file, KIOTTY_NET_MAX_PATH_LENGTH);
            const TextLength key_length = measureText(key_file, KIOTTY_NET_MAX_PATH_LENGTH);
            const TextLength password_length = measurePassword(key_password);
            const bool is_valid = cert_length && key_length && password_length;
            if (!is_valid) { return IdentityResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            const char* password = key_password == nullptr ? "" : key_password;
            const IdentityText text{ cert_file,         key_file,          password,
                                     cert_length.value, key_length.value, password_length.value };
            return IdentityResult::success(text);
        }

        TextLength measurePassword(const char* key_password)
        {
            if (key_password == nullptr) { return TextLength::success(0); }
            const size_t length = strnlen(key_password, KIOTTY_NET_MAX_PASSWORD_LENGTH);
            if (length == KIOTTY_NET_MAX_PASSWORD_LENGTH)
            {
                return TextLength::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            return TextLength::success(length);
        }

        TextLength measureTrust(KiottyNetClientAuthentication mode, const char* path)
        {
            switch (mode)
            {
            case KIOTTY_NET_CLIENT_AUTHENTICATION_NONE:
                if (path == nullptr) { return TextLength::success(0); }
                return TextLength::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            case KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL:
            case KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED:
                return measureText(path, KIOTTY_NET_MAX_PATH_LENGTH);
            }
            return TextLength::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
        }

        void writeIdentity(KiottyNetTlsIdentity& identity, const IdentityText& text)
        {
            const Span<char> cert{ identity.cert_file, sizeof(identity.cert_file) };
            const Span<char> key{ identity.key_file, sizeof(identity.key_file) };
            const Span<char> password{ identity.key_password, sizeof(identity.key_password) };
            writeText(cert, text.cert_file, text.cert_length);
            writeText(key, text.key_file, text.key_length);
            writeText(password, text.key_password, text.password_length);
        }

        uint32_t versionsOf(TlsVersions versions)
        {
            const uint32_t v12 = versions.tls_1_2 ? VERSION_V12_BIT : 0u;
            const uint32_t v13 = versions.tls_1_3 ? VERSION_V13_BIT : 0u;
            return v12 | v13;
        }

        bool isSuiteSetValid(uint32_t suites, uint32_t known)
        {
            const bool has_unknown = (suites & ~known) != 0;
            return suites != 0 && !has_unknown;
        }
    }
}
