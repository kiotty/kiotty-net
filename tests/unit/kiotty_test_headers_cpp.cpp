#include <gtest/gtest.h>

#include <cstddef>
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
}

TEST(Headers, CAndCppSeeTheSameEnumeratorValues)
{
    KiottyTestHeaderLayout c = kiottyTestHeaderLayoutFromC();
    EXPECT_EQ(c.last_code, static_cast<int>(KIOTTY_NET_CODE_INVALID_ENCODING));
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_CODE_OUT_OF_MEMORY), 24);
    EXPECT_EQ(static_cast<int>(KIOTTY_NET_CODE_INVALID_ENCODING), 25);
    EXPECT_EQ(c.last_action_type, static_cast<int>(KIOTTY_NET_ACTION_TYPE_CLOSE));
    EXPECT_EQ(c.last_tls_verification, static_cast<int>(KIOTTY_NET_TLS_VERIFICATION_NONE));
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
    EXPECT_TRUE((std::is_constructible<bool, const KiottyNet_WebSocketServer&>::value));
    EXPECT_FALSE((std::is_convertible<const KiottyNet_WebSocketClient&, bool>::value));
}
