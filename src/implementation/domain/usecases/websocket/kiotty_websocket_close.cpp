#include "domain/usecases/websocket/kiotty_websocket_close.h"

#include "core/kiotty_text.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   STATUS_BYTES = 2;
        constexpr uint32_t BITS_PER_BYTE = 8;
        constexpr uint16_t BYTE_MASK = 0xFF;
        constexpr uint16_t FIRST_DEFINED = 1000;
        constexpr uint16_t LAST_DEFINED = 1014;
        constexpr uint16_t FIRST_PRIVATE = 3000;
        constexpr uint16_t LAST_PRIVATE = 4999;
        constexpr uint16_t RESERVED_1004 = 1004;
        constexpr uint16_t NO_STATUS = KIOTTY_NET_WEB_SOCKET_STATUS_NO_STATUS;
        constexpr uint16_t ABNORMAL = KIOTTY_NET_WEB_SOCKET_STATUS_ABNORMAL_CLOSURE;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status PROTOCOL_ERROR = KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;
        constexpr Status INVALID_PAYLOAD = KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD;
    }

    ClosePayloadResult parseClosePayload(Span<const char> payload)
    {
        const Span<const char> no_reason{ nullptr, 0 };
        const ClosePayload unspecified{ no_reason, NO_STATUS };
        if (payload.empty()) { return ClosePayloadResult::success(unspecified); }
        const bool is_status_cut = payload.length < STATUS_BYTES;
        const bool is_too_long = payload.length > MAX_CONTROL_PAYLOAD_BYTES;
        if (is_status_cut || is_too_long) { return ClosePayloadResult::failure(PROTOCOL_ERROR); }
        const uint16_t high = static_cast<uint8_t>(payload[0]);
        const uint16_t low = static_cast<uint8_t>(payload[1]);
        const uint16_t status = static_cast<uint16_t>((high << BITS_PER_BYTE) | low);
        if (!isSendableCloseStatus(status)) { return ClosePayloadResult::failure(PROTOCOL_ERROR); }
        const Span<const char> reason = payload.skip(STATUS_BYTES);
        if (!isUtf8(reason)) { return ClosePayloadResult::failure(INVALID_PAYLOAD); }
        return ClosePayloadResult::success(ClosePayload{ reason, status });
    }

    CloseTextResult buildClosePayload(uint16_t status, Span<const char> reason)
    {
        const bool is_status = isSendableCloseStatus(status);
        const bool is_short = reason.length <= MAX_CONTROL_PAYLOAD_BYTES - STATUS_BYTES;
        const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;
        if (!is_status || !is_short) { return CloseTextResult::failure(invalid); }
        if (!isUtf8(reason)) { return CloseTextResult::failure(KIOTTY_NET_CODE_INVALID_ENCODING); }
        const char bytes[STATUS_BYTES] = {
            static_cast<char>(static_cast<uint8_t>(status >> BITS_PER_BYTE)),
            static_cast<char>(static_cast<uint8_t>(status & BYTE_MASK)),
        };
        CloseText text;
        (void)text.assign(Span<const char>{ bytes, STATUS_BYTES });
        (void)text.append(reason);
        return CloseTextResult::success(text);
    }

    bool isSendableCloseStatus(uint16_t status)
    {
        const bool is_defined = status >= FIRST_DEFINED && status <= LAST_DEFINED;
        const bool is_unset = status == NO_STATUS || status == ABNORMAL;
        const bool is_reserved = status == RESERVED_1004 || is_unset;
        const bool is_private = status >= FIRST_PRIVATE && status <= LAST_PRIVATE;
        return (is_defined && !is_reserved) || is_private;
    }
}
