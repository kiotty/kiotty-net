#include "websocket/data/repository/server/kiotty_websocket_access.h"

#include "core/kiotty_text.h"
#include "core/pool/kiotty_pool_handle.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_builder.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/send/kiotty_websocket_lane.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_SOCKET_PATH_LENGTH = KIOTTY_NET_MAX_URL_LENGTH - 1;

        bool        isDataOpcode(KiottyNetWebSocketOpcode opcode);
        FrameOpcode frameOpcodeOf(KiottyNetWebSocketOpcode opcode);
        bool        isBroadcastTarget(const SocketLink& link, int32_t path);
    }

    WebSocketAccess::WebSocketAccess(const HttpRegistry& registry,
                                     WebSocketHandler& handler,
                                     size_t push_capacity)
        : _registry(registry)
        , _handler(handler)
        , _push_capacity(push_capacity)
    {
    }

    KiottyNetWebSocketInfo WebSocketAccess::info(int64_t id)
    {
        KiottyNetWebSocketInfo info{};
        const LinkResult link = openLink(id);
        if (!link) { return info; }
        const SocketState& socket = _handler.socketAt(PoolHandle::fromValue(id).index);
        if (!socket.is_open) { return info; }
        info.connection = socket.connection;
        const Span<char> protocol{ info.protocol, KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH - 1 };
        (void)protocol.copyFrom(socket.protocol.span());
        return info;
    }

    KiottyNetSpan WebSocketAccess::header(int64_t id, Span<const char> name)
    {
        const OfferResult found = upgrading(id);
        if (!found) { return KiottyNetSpan{ nullptr, 0 }; }
        const Span<const char> value = headerOf(found.value().request, name);
        return KiottyNetSpan{ value.data, value.length };
    }

    KiottyNetCode WebSocketAccess::select(int64_t id, Span<const char> protocol)
    {
        const OfferResult found = upgrading(id);
        if (!found) { return found.code; }
        return chooseProtocol(_handler, found.value(), protocol);
    }

    KiottyNetCode WebSocketAccess::send(int64_t id,
                                        KiottyNetWebSocketOpcode opcode,
                                        KiottyNetSpan data)
    {
        const KiottyNetCode checked = checkData(opcode, data);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const LinkResult link = openLink(id);
        if (!link) { return link.code; }
        if (link.value().closing.load() != 0) { return KIOTTY_NET_CODE_CLOSED; }
        const Span<const char> payload{ data.data, data.length };
        return pushFrame(*_handler.lane, link.value(), frameOpcodeOf(opcode), payload);
    }

    KiottyNetCode WebSocketAccess::close(int64_t id, uint16_t status, Span<const char> reason)
    {
        const LinkResult link = openLink(id);
        if (!link) { return link.code; }
        return pushClose(*_handler.lane, link.value(), status, reason);
    }

    KiottyNetCode WebSocketAccess::broadcast(const char* path,
                                             KiottyNetWebSocketOpcode opcode,
                                             KiottyNetSpan data)
    {
        const KiottyNetCode checked = checkData(opcode, data);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        int32_t index = NO_SOCKET_PATH;
        if (path != nullptr)
        {
            const TextResult text = parseText(path, MAX_SOCKET_PATH_LENGTH);
            if (!text) { return text.code; }
            index = _registry.find(text.value);
            if (index == NO_UPGRADE_ROUTE) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        }
        KiottyNetCode result = KIOTTY_NET_CODE_SUCCESS;
        const Span<const char> payload{ data.data, data.length };
        const FrameOpcode frame = frameOpcodeOf(opcode);
        for (SocketLink& link : _handler.links())
        {
            if (!isBroadcastTarget(link, index)) { continue; }
            const KiottyNetCode pushed = pushFrame(*_handler.lane, link, frame, payload);
            if (pushed == KIOTTY_NET_CODE_BUFFER_FULL) { result = pushed; }
        }
        return result;
    }

    WebSocketAccess::OfferResult WebSocketAccess::upgrading(int64_t id)
    {
        const PoolHandle handle = PoolHandle::fromValue(id);
        const Span<SocketLink> links = _handler.links();
        const size_t index = static_cast<size_t>(handle.index);
        const bool is_inside = handle.index >= 0 && index < links.length;
        if (!is_inside) { return OfferResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        const UpgradeSession* offer = _handler.socketAt(handle.index).upgrading;
        const bool is_offer = offer != nullptr && offer->id.value() == id;
        if (is_offer) { return OfferResult::success(*offer); }
        const bool is_open = isOpenSocket(links[index], id);
        return OfferResult::failure(is_open ? KIOTTY_NET_CODE_BUSY : KIOTTY_NET_CODE_PEER_CLOSED);
    }

    WebSocketAccess::LinkResult WebSocketAccess::openLink(int64_t id)
    {
        const PoolHandle handle = PoolHandle::fromValue(id);
        const Span<SocketLink> links = _handler.links();
        const size_t index = static_cast<size_t>(handle.index);
        const bool is_inside = handle.index >= 0 && index < links.length;
        if (!is_inside) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        SocketLink& link = links[index];
        if (!isOpenSocket(link, id)) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        return LinkResult::success(link);
    }

    KiottyNetCode WebSocketAccess::checkData(KiottyNetWebSocketOpcode opcode,
                                             KiottyNetSpan data) const
    {
        const bool is_empty = data.data == nullptr || data.length == 0;
        if (is_empty || !isDataOpcode(opcode)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_masked = _handler.role == SocketRole::CLIENT;
        const size_t mask_bytes = is_masked ? MASK_KEY_BYTES : 0;
        const size_t frame_head = buildFrameHead(FrameOpcode::BINARY, data.length).length;
        const size_t head = frame_head + mask_bytes;
        const size_t room = _push_capacity;
        const bool fits = head <= room && data.length <= room - head;
        if (!fits) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    namespace
    {
        bool isDataOpcode(KiottyNetWebSocketOpcode opcode)
        {
            return opcode == KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT
                   || opcode == KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY;
        }

        FrameOpcode frameOpcodeOf(KiottyNetWebSocketOpcode opcode)
        {
            const bool is_text = opcode == KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT;
            return is_text ? FrameOpcode::TEXT : FrameOpcode::BINARY;
        }

        bool isBroadcastTarget(const SocketLink& link, int32_t path)
        {
            const bool is_open = link.socket.load() != 0 && link.closing.load() == 0;
            if (!is_open) { return false; }
            return path == NO_SOCKET_PATH || link.path.load() == path;
        }
    }
}
