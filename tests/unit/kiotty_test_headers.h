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
    int    last_code;
    int    last_action_type;
} KiottyTestHeaderLayout;

KiottyTestHeaderLayout kiottyTestHeaderLayoutFromC(void);

#ifdef __cplusplus
}
#endif

#endif
