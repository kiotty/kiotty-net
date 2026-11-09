#if KIOTTY_NET_USE_TLS

#include "data/infra/tls/backend/kiotty_openssl_session.h"

#include "core/kiotty_fixed_string.h"

#include <openssl/err.h>
#include <openssl/x509_vfy.h>


namespace kiotty::net::implementation
{
    namespace
    {
        const char* const CLOSE_NOTIFY_MESSAGE = "tls: close_notify from peer";
        const char* const UNKNOWN_ERROR_MESSAGE = "tls: unknown error";

        using HostName = FixedString<KIOTTY_NET_MAX_HOST_LENGTH>;

        SSL*       discardSsl(SSL* ssl);
        bool       isServerName(Span<const char> hostname);
        bool       isIpLiteral(Span<const char> hostname);
        bool       isAddressCharacter(char character);
        TlsFailure verificationFailure(const SSL* ssl);
    }

    SSL* reuseSsl(SSL* ssl, SSL_CTX* context)
    {
        if (ssl == nullptr) { return nullptr; }
        const SSL_CTX* owner = SSL_get_SSL_CTX(ssl);
        if (owner != context) { return discardSsl(ssl); }
        const int cleared = SSL_clear(ssl);
        if (cleared != 1) { return discardSsl(ssl); }
        return ssl;
    }

    void startRole(SSL* ssl, TlsSettings settings)
    {
        if (settings.is_server)
        {
            SSL_set_accept_state(ssl);
            return;
        }
        SSL_set_connect_state(ssl);
    }

    KiottyNetCode setHostname(SSL* ssl, TlsSettings settings, Span<const char> hostname)
    {
        HostName name;
        const bool is_assigned = name.assign(hostname);
        if (!is_assigned) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (isServerName(name.span()))
        {
            SSL_set_tlsext_host_name(ssl, name.c_str());
        }
        SSL_set1_host(ssl, nullptr);
        const bool is_verified = !name.empty() && settings.has_hostname_verification;
        if (is_verified)
        {
            SSL_set1_host(ssl, name.c_str());
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isRetryable(int reason)
    {
        return reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE;
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

    const char* lastErrorMessage()
    {
        const unsigned long error_code = ERR_peek_last_error();
        const char* reason = ERR_reason_error_string(error_code);
        if (reason == nullptr) { return UNKNOWN_ERROR_MESSAGE; }
        return reason;
    }

    namespace
    {
        SSL* discardSsl(SSL* ssl)
        {
            SSL_free(ssl);
            return nullptr;
        }

        bool isServerName(Span<const char> hostname)
        {
            if (hostname.empty()) { return false; }
            const bool is_literal = isIpLiteral(hostname);
            return !is_literal;
        }

        bool isIpLiteral(Span<const char> hostname)
        {
            bool is_digits_and_dots = true;
            for (const char character : hostname)
            {
                if (character == ':') { return true; }
                const bool is_address = isAddressCharacter(character);
                is_digits_and_dots = is_digits_and_dots && is_address;
            }
            return is_digits_and_dots;
        }

        bool isAddressCharacter(char character)
        {
            const bool is_digit = character >= '0' && character <= '9';
            return is_digit || character == '.';
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
    }
}

#endif
