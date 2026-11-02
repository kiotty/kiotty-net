#if !defined(KIOTTY_TLS_ENGINE_H)
#define KIOTTY_TLS_ENGINE_H

#include "base/tls/kiotty_tls_profile.h"
#include "core/kiotty_result.h"
#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

struct ssl_st;
struct bio_st;

namespace kiotty
{
    namespace net
    {
        enum class TlsStep : uint8_t
        {
            SEND,
            RECEIVE,
            DONE,
            FAILED
        };

        typedef Result<KiottyNetCode, size_t> DecodeResult;

        size_t tlsCipherBound(size_t plain_length);
        size_t tlsCipherCapacity(size_t plain_capacity);

        class TlsEngine
        {
        public:
            TlsEngine(size_t receive_capacity, size_t send_capacity);
            ~TlsEngine();

            TlsEngine(const TlsEngine&) = delete;
            TlsEngine& operator=(const TlsEngine&) = delete;

            KiottyNetCode open(TlsProfile& profile, const char* hostname);
            void          close();
            bool          active() const;

            Stage& cipherReceive();
            Stage& cipherSend();

            TlsStep      handshake();
            DecodeResult decode(Stage& plain);
            void         encode(KiottyNetSpan plain);

            KiottyNetCode lastCode() const;
            const char*   lastMessage() const;

#if defined(KIOTTY_NET_TESTING)
            void debugKeyUpdate();
            void debugShutdown();
#endif

        private:
            TlsStep fail(KiottyNetCode code, const char* message);
            void    feed();
            void    flush();
            size_t  pending() const;

            Stage         _cipher_receive;
            Stage         _cipher_send;
            ssl_st*       _ssl;
            bio_st*       _receive_bio;
            bio_st*       _send_bio;
            bool          _active;
            bool          _failed;
            KiottyNetCode _last_code;
            const char*   _last_message;
        };
    }
}

#endif
