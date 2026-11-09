#pragma once

#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetCode initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration& configuration,
                                                   const char* certificate_file,
                                                   const char* key_file,
                                                   const char* key_password);
    KiottyNetCode configureTlsServerVersions(KiottyNetTlsServerConfiguration& configuration,
                                             uint32_t versions);
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
                                             uint32_t versions);
    KiottyNetCode configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientTrustStore(KiottyNetTlsClientConfiguration& configuration,
                                               const char* trust_store_path);
    KiottyNetCode configureTlsClientVerification(KiottyNetTlsClientConfiguration& configuration,
                                                 KiottyNetTlsVerification verification);
    KiottyNetCode configureTlsClientIdentity(KiottyNetTlsClientConfiguration& configuration,
                                             const char* certificate_file,
                                             const char* key_file,
                                             const char* key_password);

    bool isInitialized(const KiottyNetTlsServerConfiguration& configuration);
    bool isInitialized(const KiottyNetTlsClientConfiguration& configuration);
}
