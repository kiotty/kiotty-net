#include "context/kiotty_tls_configuration.h"

#include "context/kiotty_configuration.h"
#include "core/kiotty_result.h"

#include <cstddef>

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

        bool isIdentityValid(const TlsIdentityText& text);
        void writeIdentity(KiottyNetTlsIdentity& identity, const TlsIdentityText& text);
        bool isTrustAccepted(KiottyNetClientAuthentication mode, Span<const char> path);
        bool writePath(char (&field)[KIOTTY_NET_MAX_PATH_LENGTH], Span<const char> path);
        bool isSuiteSetValid(uint32_t suites, uint32_t known);
        bool isVerificationKnown(KiottyNetTlsVerification verification);
    }

    KiottyNetCode initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration& configuration,
                                                   const TlsIdentityText& identity)
    {
        if (!isIdentityValid(identity)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration = KiottyNetTlsServerConfiguration{};
        configuration.initialization_mark = INITIALIZATION_MARK;
        configuration.versions = ALL_VERSIONS;
        configuration.v12_cipher_suites = ALL_V12_SUITES;
        configuration.v13_cipher_suites = ALL_V13_SUITES;
        configuration.client_authentication = KIOTTY_NET_CLIENT_AUTHENTICATION_NONE;
        writeIdentity(configuration.identity, identity);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsServerVersions(KiottyNetTlsServerConfiguration& configuration,
                                             uint32_t versions)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(versions, ALL_VERSIONS)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.versions = versions;
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
        Span<const char> trust_store_path)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_accepted = isTrustAccepted(mode, trust_store_path);
        if (!is_accepted) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool written = writePath(configuration.trust_store_path, trust_store_path);
        if (!written) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.client_authentication = mode;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration& configuration)
    {
        configuration = KiottyNetTlsClientConfiguration{};
        configuration.initialization_mark = INITIALIZATION_MARK;
        configuration.versions = ALL_VERSIONS;
        configuration.v12_cipher_suites = ALL_V12_SUITES;
        configuration.v13_cipher_suites = ALL_V13_SUITES;
        configuration.verification = KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientVersions(KiottyNetTlsClientConfiguration& configuration,
                                             uint32_t versions)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isSuiteSetValid(versions, ALL_VERSIONS)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.versions = versions;
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
                                               Span<const char> trust_store_path)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (trust_store_path.empty()) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool written = writePath(configuration.trust_store_path, trust_store_path);
        if (!written) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientVerification(KiottyNetTlsClientConfiguration& configuration,
                                                 KiottyNetTlsVerification verification)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isVerificationKnown(verification)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        configuration.verification = verification;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureTlsClientIdentity(KiottyNetTlsClientConfiguration& configuration,
                                             const TlsIdentityText& identity)
    {
        if (!isInitialized(configuration)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isIdentityValid(identity)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        writeIdentity(configuration.identity, identity);
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
        bool isIdentityValid(const TlsIdentityText& text)
        {
            const size_t path_limit = KIOTTY_NET_MAX_PATH_LENGTH;
            const size_t password_limit = KIOTTY_NET_MAX_PASSWORD_LENGTH;
            const bool is_named = !text.certificate_file.empty() && !text.key_file.empty();
            const bool is_certificate_fitting = text.certificate_file.length < path_limit;
            const bool is_key_fitting = text.key_file.length < path_limit;
            const bool is_password_fitting = text.key_password.length < password_limit;
            return is_named && is_certificate_fitting && is_key_fitting && is_password_fitting;
        }

        void writeIdentity(KiottyNetTlsIdentity& identity, const TlsIdentityText& text)
        {
            const Span<char> password{ identity.key_password, sizeof(identity.key_password) };
            static_cast<void>(writePath(identity.certificate_file, text.certificate_file));
            static_cast<void>(writePath(identity.key_file, text.key_file));
            static_cast<void>(writeText(password, text.key_password));
        }

        bool isTrustAccepted(KiottyNetClientAuthentication mode, Span<const char> path)
        {
            switch (mode)
            {
            case KIOTTY_NET_CLIENT_AUTHENTICATION_NONE:
                return path.empty();
            case KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL:
            case KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED:
                return !path.empty();
            }
            return false;
        }

        bool writePath(char (&field)[KIOTTY_NET_MAX_PATH_LENGTH], Span<const char> path)
        {
            return writeText(Span<char>{ field, KIOTTY_NET_MAX_PATH_LENGTH }, path);
        }

        bool isSuiteSetValid(uint32_t suites, uint32_t known)
        {
            const bool has_unknown = (suites & ~known) != 0;
            return suites != 0 && !has_unknown;
        }

        bool isVerificationKnown(KiottyNetTlsVerification verification)
        {
            switch (verification)
            {
            case KIOTTY_NET_TLS_VERIFICATION_NONE:
            case KIOTTY_NET_TLS_VERIFICATION_SERVER:
            case KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME:
                return true;
            }
            return false;
        }
    }
}
