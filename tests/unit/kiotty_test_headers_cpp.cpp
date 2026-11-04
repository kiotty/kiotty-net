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
    EXPECT_EQ(c.server_config, sizeof(KiottyNetServerConfig));
    EXPECT_EQ(c.client_config, sizeof(KiottyNetClientConfig));
    EXPECT_EQ(c.tls_server_config, sizeof(KiottyNetTlsServerConfig));
    EXPECT_EQ(c.tls_client_config, sizeof(KiottyNetTlsClientConfig));
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
    EXPECT_EQ(c.last_code, static_cast<int>(KIOTTY_OUT_OF_MEMORY));
    EXPECT_EQ(c.last_action_type, static_cast<int>(KIOTTY_ACTION_CLOSE));
}

TEST(Headers, TransitionCarriesAConnectionAction)
{
    KiottyNetTransition transition = {};
    KiottyNetConnectionAction& action = transition.entry_action;
    action.type = KIOTTY_ACTION_WRITE;
    EXPECT_EQ(transition.entry_action.type, KIOTTY_ACTION_WRITE);
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
    KiottyNetConnectionOwner* first  = reinterpret_cast<KiottyNetConnectionOwner*>(&anchor);
    KiottyNetConnectionOwner* second = nullptr;
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
