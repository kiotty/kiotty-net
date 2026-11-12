#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"

#include "kiotty_test_headers.h"

#include <stddef.h>

static const KiottyNetRule RULES[] = {
    KIOTTY_NET_RULE(0, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, NULL, 1, 2),
    KIOTTY_NET_RULE_UNCHECKED(1, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED, NULL)
};

KiottyTestHeaderLayout kiottyTestHeaderLayoutFromC(void)
{
    KiottyTestHeaderLayout layout;
    layout.code                 = sizeof(KiottyNetCode);
    layout.connection_action    = sizeof(KiottyNetConnectionAction);
    layout.connection_decision  = sizeof(KiottyNetConnectionDecision);
    layout.connection_handler   = sizeof(KiottyNetConnectionHandler);
    layout.connection_info      = sizeof(KiottyNetConnectionInfo);
    layout.connection_handle    = sizeof(KiottyNetConnectionHandle);
    layout.connection_stream    = sizeof(KiottyNetConnectionStream);
    layout.connection_handle_id_offset = offsetof(KiottyNetConnectionHandle, id);
    layout.connection_stream_id_offset = offsetof(KiottyNetConnectionStream, id);
    layout.server_config        = sizeof(KiottyNetServerConfiguration);
    layout.client_config        = sizeof(KiottyNetClientConfiguration);
    layout.tls_server_config    = sizeof(KiottyNetTlsServerConfiguration);
    layout.tls_client_config    = sizeof(KiottyNetTlsClientConfiguration);
    layout.context_result       = sizeof(KiottyNetContextResult);
    layout.server_result        = sizeof(KiottyNetServerResult);
    layout.event_client_result  = sizeof(KiottyNetEventClientResult);
    layout.stream_client_result = sizeof(KiottyNetStreamClientResult);
    layout.transition           = sizeof(KiottyNetTransition);
    layout.rule                 = sizeof(KiottyNetRule);
    layout.http_limits          = sizeof(KiottyNetHttpLimits);
    layout.http_request         = sizeof(KiottyNetHttpRequest);
    layout.http_response_handle = sizeof(KiottyNetHttpResponseHandle);
    layout.http_work_handle     = sizeof(KiottyNetHttpWorkHandle);
    layout.http_route           = sizeof(KiottyNetHttpRoute);
    layout.http_server_result   = sizeof(KiottyNetHttpServerResult);
    layout.http_request_keep_alive = offsetof(KiottyNetHttpRequest, keep_alive);
    layout.rule_next_count      = RULES[0].next.count;
    layout.rule_second_next     = RULES[0].next.at[1];
    layout.last_tls_verification = KIOTTY_NET_TLS_VERIFICATION_NONE;
    layout.last_code            = KIOTTY_NET_CODE_INVALID_ENCODING;
    layout.last_action_type     = KIOTTY_NET_ACTION_TYPE_CLOSE;
    layout.http_content_type_count = KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT;
    layout.http_status_header_fields_too_large = KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE;
    return layout;
}
