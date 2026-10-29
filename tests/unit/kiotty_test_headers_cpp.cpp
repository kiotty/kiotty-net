#include <gtest/gtest.h>

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
    EXPECT_EQ(c.last_code, static_cast<int>(KIOTTY_TLS_RECORD_ERROR));
    EXPECT_EQ(c.last_action_type, static_cast<int>(KIOTTY_ACTION_CLOSE));
}

TEST(Headers, TransitionCarriesAConnectionAction)
{
    KiottyNetTransition transition = {};
    KiottyNetConnectionAction& action = transition.entry_action;
    action.type = KIOTTY_ACTION_WRITE;
    EXPECT_EQ(transition.entry_action.type, KIOTTY_ACTION_WRITE);
}
