#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"

#include "kiotty_test_headers.h"

KiottyTestHeaderLayout kiottyTestHeaderLayoutFromC(void)
{
    KiottyTestHeaderLayout layout;
    layout.code                 = sizeof(KiottyNetCode);
    layout.connection_action    = sizeof(KiottyNetConnectionAction);
    layout.connection_decision  = sizeof(KiottyNetConnectionDecision);
    layout.connection_handler   = sizeof(KiottyNetConnectionHandler);
    layout.connection_info      = sizeof(KiottyNetConnectionInfo);
    layout.server_config        = sizeof(KiottyNetServerConfig);
    layout.client_config        = sizeof(KiottyNetClientConfig);
    layout.tls_server_config    = sizeof(KiottyNetTlsServerConfig);
    layout.tls_client_config    = sizeof(KiottyNetTlsClientConfig);
    layout.context_result       = sizeof(KiottyNetContextResult);
    layout.server_result        = sizeof(KiottyNetServerResult);
    layout.event_client_result  = sizeof(KiottyNetEventClientResult);
    layout.stream_client_result = sizeof(KiottyNetStreamClientResult);
    layout.transition           = sizeof(KiottyNetTransition);
    layout.rule                 = sizeof(KiottyNetRule);
    layout.last_code            = KIOTTY_TLS_RECORD_ERROR;
    layout.last_action_type     = KIOTTY_ACTION_CLOSE;
    return layout;
}
