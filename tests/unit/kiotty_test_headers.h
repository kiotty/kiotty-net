#ifndef KIOTTY_TEST_HEADERS_H
#define KIOTTY_TEST_HEADERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KiottyTestHeaderLayout
{
    size_t code;
    size_t connection_action;
    size_t connection_decision;
    size_t connection_handler;
    size_t connection_info;
    size_t connection_handle;
    size_t connection_stream;
    size_t connection_handle_id_offset;
    size_t connection_stream_id_offset;
    size_t server_config;
    size_t client_config;
    size_t tls_server_config;
    size_t tls_client_config;
    size_t context_result;
    size_t server_result;
    size_t event_client_result;
    size_t stream_client_result;
    size_t transition;
    size_t rule;
    size_t http_limits;
    size_t http_request;
    size_t http_response_handle;
    size_t http_work_handle;
    size_t http_route;
    size_t http_server_result;
    size_t http_request_keep_alive;
    size_t rule_next_count;
    int    rule_second_next;
    int    last_tls_verification;
    int    last_code;
    int    last_action_type;
    int    http_content_type_count;
    int    http_status_header_fields_too_large;
} KiottyTestHeaderLayout;

KiottyTestHeaderLayout kiottyTestHeaderLayoutFromC(void);

#ifdef __cplusplus
}
#endif

#endif
