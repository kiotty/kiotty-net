#include "websocket/data/state/kiotty_websocket_client_state.h"

namespace kiotty::net::implementation
{
    WebSocketClientState::WebSocketClientState()
        : reply(Span<char>{ nullptr, 0 }, Span<KiottyNetHttpHeader>{ nullptr, 0 })
        , response(Span<char>{ nullptr, 0 })
        , request{}
        , info{}
        , message{ nullptr, 0 }
        , limits{}
        , receive_capacity(0)
        , push_capacity(0)
        , app_context(nullptr)
        , on_error(nullptr)
        , handler(nullptr)
        , id{ 0, 0 }
        , header_deadline(0)
        , sent(0)
    {
    }

    UpgradeSession clientUpgradeOf(WebSocketClientState& state)
    {
        return UpgradeSession{
            state.request,
            state.info,
            state.response,
            state.message,
            state.limits,
            state.receive_capacity,
            state.app_context,
            state.id,
            false,
        };
    }
}
