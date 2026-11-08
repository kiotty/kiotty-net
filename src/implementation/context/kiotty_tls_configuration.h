#pragma once

#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct TlsVersions
    {
        bool tls_1_2;
        bool tls_1_3;
    };

    static_assert(sizeof(TlsVersions) == 2, "TlsVersions: unexpected padding");

    struct TlsVerification
    {
        bool verify_server;
        bool verify_hostname;
    };

    static_assert(sizeof(TlsVerification) == 2, "TlsVerification: unexpected padding");

    KiottyNetCode initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration& configuration,
                                                   const char* cert_file,
                                                   const char* key_file,
                                                   const char* key_password);
    KiottyNetCode configureTlsServerVersions(KiottyNetTlsServerConfiguration& configuration,
                                             TlsVersions versions);
    KiottyNetCode configureTlsServerV12CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsServerV13CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsServerClientAuthentication(
        KiottyNetTlsServerConfiguration& configuration,
        KiottyNetClientAuthentication mode,
        const char* trust_store_path);

    KiottyNetCode initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration& configuration);
    KiottyNetCode configureTlsClientVersions(KiottyNetTlsClientConfiguration& configuration,
                                             TlsVersions versions);
    KiottyNetCode configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientTrustStore(KiottyNetTlsClientConfiguration& configuration,
                                               const char* trust_store_path);
    KiottyNetCode configureTlsClientVerification(KiottyNetTlsClientConfiguration& configuration,
                                                 TlsVerification verification);
    KiottyNetCode configureTlsClientIdentity(KiottyNetTlsClientConfiguration& configuration,
                                             const char* cert_file,
                                             const char* key_file,
                                             const char* key_password);

    bool isInitialized(const KiottyNetTlsServerConfiguration& configuration);
    bool isInitialized(const KiottyNetTlsClientConfiguration& configuration);
}
