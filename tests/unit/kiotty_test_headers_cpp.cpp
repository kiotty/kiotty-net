#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <type_traits>

#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"

#include "kiotty_test_headers.h"

TEST(Headers, CAndCppSeeTheSameLayoutForEveryPublicStruct)
{
    KiottyTestHeaderLayout c = kiottyTestHeaderLayoutFromC();
    EXPECT_EQ(c.code, sizeof(KiottyNetCode));
    EXPECT_EQ(c.connection_action, sizeof(KiottyNetConnectionAction));
    EXPECT_EQ(c.connection_decision, sizeof(KiottyNetConnectionDecision));
    EXPECT_EQ(c.connection_handler, sizeof(KiottyNetConnectionHandler));
    EXPECT_EQ(c.connection_info, sizeof(KiottyNetConnectionInfo));
    EXPECT_EQ(c.connection_handle, sizeof(KiottyNetConnectionHandle));
    EXPECT_EQ(c.connection_stream, sizeof(KiottyNetConnectionStream));
    EXPECT_EQ(c.connection_handle_id_offset, offsetof(KiottyNetConnectionHandle, id));
    EXPECT_EQ(c.connection_stream_id_offset, offsetof(KiottyNetConnectionStream, id));
    EXPECT_EQ(c.server_config, sizeof(KiottyNetServerConfiguration));
    EXPECT_EQ(c.client_config, sizeof(KiottyNetClientConfiguration));
    EXPECT_EQ(c.tls_server_config, sizeof(KiottyNetTlsServerConfiguration));
    EXPECT_EQ(c.tls_client_config, sizeof(KiottyNetTlsClientConfiguration));
    EXPECT_EQ(c.context_result, sizeof(KiottyNetContextResult));
    EXPECT_EQ(c.server_result, sizeof(KiottyNetServerResult));
    EXPECT_EQ(c.event_client_result, sizeof(KiottyNetEventClientResult));
    EXPECT_EQ(c.stream_client_result, sizeof(KiottyNetStreamClientResult));
    EXPECT_EQ(c.transition, sizeof(KiottyNetTransition));
    EXPECT_EQ(c.rule, sizeof(KiottyNetRule));
    EXPECT_EQ(c.http_limits, sizeof(KiottyNetHttpLimits));
    EXPECT_EQ(c.http_request, sizeof(KiottyNetHttpRequest));
    EXPECT_EQ(c.http_response_handle, sizeof(KiottyNetHttpResponseHandle));
    EXPECT_EQ(c.http_work_handle, sizeof(KiottyNetHttpWorkHandle));
    EXPECT_EQ(c.http_route, sizeof(KiottyNetHttpRoute));
    EXPECT_EQ(c.http_server_result, sizeof(KiottyNetHttpServerResult));
    EXPECT_EQ(c.websocket_handle, sizeof(KiottyNetWebSocketHandle));
    EXPECT_EQ(c.websocket_handle_id_offset, offsetof(KiottyNetWebSocketHandle, id));
    EXPECT_EQ(c.websocket_client_result, sizeof(KiottyNetWebSocketClientResult));
    EXPECT_EQ(c.websocket_route, sizeof(KiottyNetWebSocketRoute));
    EXPECT_EQ(c.websocket_route_index_offset, offsetof(KiottyNetWebSocketRoute, index));
    EXPECT_EQ(c.http_request_keep_alive, offsetof(KiottyNetHttpRequest, keep_alive));
}

TEST(Headers, CAndCppSeeTheSameEnumeratorValues)
{
    KiottyTestHeaderLayout c = kiottyTestHeaderLayoutFromC();
    EXPECT_EQ(c.last_code, static_cast<int>(KIOTTY_NET_CODE_INVALID_ENCODING));
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_CODE_OUT_OF_MEMORY), 24);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_CODE_INVALID_ENCODING), 25);
    EXPECT_EQ(c.last_action_type, static_cast<int>(KIOTTY_NET_ACTION_TYPE_CLOSE));
    EXPECT_EQ(c.last_tls_verification, static_cast<int>(KIOTTY_NET_TLS_VERIFICATION_NONE));
    EXPECT_EQ(c.http_content_type_count, static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT));
    EXPECT_EQ(c.http_status_header_fields_too_large, 431);
}

TEST(Headers, HttpRequestKeepAliveIsTheAbiBool)
{
    EXPECT_TRUE((std::is_same<decltype(KiottyNetHttpRequest::keep_alive), KiottyNetBool>::value));
    EXPECT_EQ(sizeof(KiottyNetBool), sizeof(int32_t));
}

TEST(Headers, DisconnectCallbacksCarryNoConnectionHandle)
{
    typedef void (*BaseDisconnected)(KiottyNetAppContext, KiottyNetConnectionContext, KiottyNetCode);
    typedef void (*StatefulRelease)(KiottyNetAppContext, KiottyNetConnectionContext);
    typedef void (*HttpDisconnected)(KiottyNetAppContext, KiottyNetConnectionInfo, KiottyNetCode);
    EXPECT_TRUE((std::is_same<decltype(KiottyNetConnectionHandler::onDisconnected), BaseDisconnected>::value));
    EXPECT_TRUE((std::is_same<decltype(KiottyNetStatefulAppContext::releaseConnectionContext), StatefulRelease>::value));
    EXPECT_TRUE((std::is_same<KiottyNetHttpDisconnectedAction, HttpDisconnected>::value));
}

TEST(Headers, HttpContentTypesEndWithByExtensionThenCustom)
{
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM), 0);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION) + 1,
              static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM));
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM) + 1,
              static_cast<int>(KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT));
}

TEST(Headers, HttpStatusEnumeratorsAreTheWireCodes)
{
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS), 101);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_STATUS_ACCEPTED), 202);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_STATUS_NOT_ACCEPTABLE), 406);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_HTTP_STATUS_SERVICE_UNAVAILABLE), 503);
}

TEST(Headers, CRuleMacroAndCppRuleFillTheSameNextStates)
{
    KiottyTestHeaderLayout c = kiottyTestHeaderLayoutFromC();
    constexpr kiotty::net::Rule rule =
        kiotty::net::From(0).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(nullptr).to(1, 2);
    EXPECT_EQ(c.rule_next_count, static_cast<size_t>(rule.raw.next.count));
    EXPECT_EQ(c.rule_second_next, rule.raw.next.at[1]);
}

TEST(Headers, ZeroedTlsClientConfigurationVerifiesServerAndHostname)
{
    KiottyNetTlsClientConfiguration configuration = {};
    EXPECT_EQ(configuration.verification, KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME);
}

TEST(Headers, TransitionCarriesAConnectionAction)
{
    KiottyNetTransition transition = {};
    KiottyNetConnectionAction& action = transition.entry_action;
    action.type = KIOTTY_NET_ACTION_TYPE_WRITE;
    EXPECT_EQ(transition.entry_action.type, KIOTTY_NET_ACTION_TYPE_WRITE);
}

TEST(Headers, ConnectionHandleAndStreamAreDistinctTypesWithTheSameFields)
{
    EXPECT_FALSE((std::is_same<KiottyNetConnectionHandle, KiottyNetConnectionStream>::value));
    EXPECT_FALSE((std::is_convertible<KiottyNetConnectionHandle, KiottyNetConnectionStream>::value));
    EXPECT_EQ(sizeof(KiottyNetConnectionHandle), sizeof(KiottyNetConnectionStream));
}

TEST(Headers, ConnectionWrappersCompareByOwnerThenId)
{
    int                       anchor = 0;
    KiottyNetEndpoint* first  = reinterpret_cast<KiottyNetEndpoint*>(&anchor);
    KiottyNetEndpoint* second = nullptr;
    KiottyNetConnectionHandle a = { first, 7 };
    KiottyNetConnectionHandle b = { first, 7 };
    KiottyNetConnectionHandle c = { first, 8 };
    KiottyNetConnectionHandle d = { second, 7 };

    EXPECT_TRUE(KiottyNet_Connection(a) == KiottyNet_Connection(b));
    EXPECT_TRUE(KiottyNet_Connection(a) != KiottyNet_Connection(c));
    EXPECT_TRUE(KiottyNet_Connection(a) != KiottyNet_Connection(d));
    EXPECT_TRUE(KiottyNet_Connection(a) < KiottyNet_Connection(c));
    EXPECT_FALSE(KiottyNet_Connection(c) < KiottyNet_Connection(a));
    EXPECT_FALSE(KiottyNet_Connection(a) < KiottyNet_Connection(b));

    std::map<KiottyNet_Connection, int> sessions;
    sessions[KiottyNet_Connection(a)] = 1;
    sessions[KiottyNet_Connection(c)] = 2;
    sessions[KiottyNet_Connection(d)] = 3;
    sessions[KiottyNet_Connection(b)] = 4;
    EXPECT_EQ(sessions.size(), 3u);
    EXPECT_EQ(sessions[KiottyNet_Connection(a)], 4);
}

TEST(Headers, OwningWrappersTestAsBoolOnlyExplicitly)
{
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_Server&>::value));
    EXPECT_FALSE((std::is_convertible<const KiottyNet_Server&, bool>::value));
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_EventClient&>::value));
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_StreamClient&>::value));
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_Context&>::value));
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_HttpServer&>::value));
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_WebSocketClient&>::value));
    EXPECT_FALSE((std::is_convertible<const KiottyNet_WebSocketClient&, bool>::value));
}

TEST(Headers, WebSocketHandlesOrderByOwnerThenId)
{
    KiottyNetWebSocketEndpoint* owner = reinterpret_cast<KiottyNetWebSocketEndpoint*>(&owner);
    const KiottyNetWebSocketHandle first = { owner, 1 };
    const KiottyNetWebSocketHandle second = { owner, 2 };
    const KiottyNetWebSocketHandle none = { nullptr, 0 };
    EXPECT_TRUE(KiottyNet_WebSocket(first) == KiottyNet_WebSocket(first));
    EXPECT_TRUE(KiottyNet_WebSocket(first) != KiottyNet_WebSocket(second));
    EXPECT_TRUE(KiottyNet_WebSocket(first) < KiottyNet_WebSocket(second));
    EXPECT_FALSE(KiottyNet_WebSocket(second) < KiottyNet_WebSocket(first));
    EXPECT_TRUE(KiottyNet_WebSocket(none) < KiottyNet_WebSocket(first) || KiottyNet_WebSocket(first) < KiottyNet_WebSocket(none));
}
