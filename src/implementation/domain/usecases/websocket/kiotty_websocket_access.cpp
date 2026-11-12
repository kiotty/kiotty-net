#include "domain/usecases/websocket/kiotty_websocket_access.h"

#include "core/kiotty_text.h"
#include "core/pool/kiotty_pool_handle.h"
#include "domain/usecases/websocket/kiotty_websocket_lane.h"
#include "domain/usecases/websocket/kiotty_websocket_upgrade.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_SOCKET_PATH_LENGTH = KIOTTY_NET_MAX_URL_LENGTH - 1;

        bool          isDataOpcode(KiottyNetWebSocketOpcode opcode);
        FrameOpcode   frameOpcodeOf(KiottyNetWebSocketOpcode opcode);
        KiottyNetCode checkData(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
        bool          isBroadcastTarget(const SocketLink& link, int32_t path);
    }

    WebSocketAccess::WebSocketAccess(HttpSite& site)
        : _site(site)
    {
    }

    KiottyNetWebSocketInfo WebSocketAccess::info(int64_t id)
    {
        KiottyNetWebSocketInfo info{};
        const SessionResult found = _site.sessions.find(id);
        if (!found) { return info; }
        const HttpSession& session = found.value();
        if (!session.socket.is_open) { return info; }
        info.connection = session.info;
        const Span<char> protocol{ info.protocol, KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH - 1 };
        (void)protocol.copyFrom(session.socket.protocol.span());
        return info;
    }

    KiottyNetSpan WebSocketAccess::header(int64_t id, Span<const char> name)
    {
        const SessionResult found = upgrading(id);
        if (!found) { return KiottyNetSpan{ nullptr, 0 }; }
        const Span<const char> value = headerOf(found.value().request.request(), name);
        return KiottyNetSpan{ value.data, value.length };
    }

    KiottyNetCode WebSocketAccess::select(int64_t id, Span<const char> protocol)
    {
        const SessionResult found = upgrading(id);
        if (!found) { return found.code; }
        return chooseProtocol(found.value(), protocol);
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
        return pushFrame(*_site.lane, link.value(), frameOpcodeOf(opcode), payload);
    }

    KiottyNetCode WebSocketAccess::close(int64_t id, uint16_t status, Span<const char> reason)
    {
        const LinkResult link = openLink(id);
        if (!link) { return link.code; }
        return pushClose(*_site.lane, link.value(), status, reason);
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
            index = _site.registry.find(text.value);
            if (index == NO_SOCKET_PATH) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        }
        KiottyNetCode result = KIOTTY_NET_CODE_SUCCESS;
        const Span<const char> payload{ data.data, data.length };
        const FrameOpcode frame = frameOpcodeOf(opcode);
        for (SocketLink& link : _site.sessions.links())
        {
            if (!isBroadcastTarget(link, index)) { continue; }
            const KiottyNetCode pushed = pushFrame(*_site.lane, link, frame, payload);
            if (pushed == KIOTTY_NET_CODE_BUFFER_FULL) { result = pushed; }
        }
        return result;
    }

    SessionResult WebSocketAccess::upgrading(int64_t id)
    {
        const SessionResult found = _site.sessions.find(id);
        if (!found) { return found; }
        const bool is_upgrading = found.value().phase == SessionPhase::UPGRADING;
        if (!is_upgrading) { return SessionResult::failure(KIOTTY_NET_CODE_BUSY); }
        return found;
    }

    WebSocketAccess::LinkResult WebSocketAccess::openLink(int64_t id)
    {
        const PoolHandle handle = PoolHandle::fromValue(id);
        const Span<SocketLink> links = _site.sessions.links();
        const size_t index = static_cast<size_t>(handle.index);
        const bool is_inside = handle.index >= 0 && index < links.length;
        if (!is_inside) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        SocketLink& link = links[index];
        if (!isOpenSocket(link, id)) { return LinkResult::failure(KIOTTY_NET_CODE_PEER_CLOSED); }
        return LinkResult::success(link);
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

        KiottyNetCode checkData(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data)
        {
            const bool is_empty = data.data == nullptr || data.length == 0;
            if (is_empty || !isDataOpcode(opcode)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        bool isBroadcastTarget(const SocketLink& link, int32_t path)
        {
            const bool is_open = link.socket.load() != 0 && link.closing.load() == 0;
            if (!is_open) { return false; }
            return path == NO_SOCKET_PATH || link.path.load() == path;
        }
    }
}
