#include "domain/usecases/websocket/kiotty_websocket_handshake.h"

#include "core/kiotty_base64.h"
#include "core/kiotty_sha1.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char   ACCEPT_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        constexpr size_t KEY_PADDING = 2;
    }

    bool isWebSocketKey(Span<const char> key)
    {
        if (key.length != WEB_SOCKET_KEY_LENGTH) { return false; }
        if (!isBase64Text(key)) { return false; }
        const bool is_last_pad = key[WEB_SOCKET_KEY_LENGTH - 1] == '=';
        return is_last_pad && key[WEB_SOCKET_KEY_LENGTH - KEY_PADDING] == '=';
    }

    AcceptText acceptOf(Span<const char> key)
    {
        const Span<const char> guid{ ACCEPT_GUID, sizeof(ACCEPT_GUID) - 1 };
        const Sha1Digest digest = sha1Of(key, guid);
        char encoded[WEB_SOCKET_ACCEPT_LENGTH] = {};
        const Span<const uint8_t> input{ digest.bytes, SHA1_DIGEST_BYTES };
        (void)encodeBase64(input, Span<char>{ encoded, WEB_SOCKET_ACCEPT_LENGTH });
        AcceptText text;
        (void)text.assign(Span<const char>{ encoded, WEB_SOCKET_ACCEPT_LENGTH });
        return text;
    }
}
