#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct TlsIdentityText
    {
        Span<const char> certificate_file;
        Span<const char> key_file;
        Span<const char> key_password;
    };

    KiottyNetCode initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration& configuration,
                                                   const TlsIdentityText& identity);
    KiottyNetCode configureTlsServerVersions(KiottyNetTlsServerConfiguration& configuration,
                                             uint32_t versions);
    KiottyNetCode configureTlsServerV12CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsServerV13CipherSuites(KiottyNetTlsServerConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsServerClientAuthentication(
        KiottyNetTlsServerConfiguration& configuration,
        KiottyNetClientAuthentication mode,
        Span<const char> trust_store_path);

    KiottyNetCode initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration& configuration);
    KiottyNetCode configureTlsClientVersions(KiottyNetTlsClientConfiguration& configuration,
                                             uint32_t versions);
    KiottyNetCode configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration& configuration,
                                                    uint32_t suites);
    KiottyNetCode configureTlsClientTrustStore(KiottyNetTlsClientConfiguration& configuration,
                                               Span<const char> trust_store_path);
    KiottyNetCode configureTlsClientVerification(KiottyNetTlsClientConfiguration& configuration,
                                                 KiottyNetTlsVerification verification);
    KiottyNetCode configureTlsClientIdentity(KiottyNetTlsClientConfiguration& configuration,
                                             const TlsIdentityText& identity);

    bool isInitialized(const KiottyNetTlsServerConfiguration& configuration);
    bool isInitialized(const KiottyNetTlsClientConfiguration& configuration);
}
