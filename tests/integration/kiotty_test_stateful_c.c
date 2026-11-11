#include "kiotty_test_stateful_c.h"

#include <string.h>

enum
{
    C_READING_HEADER,
    C_READING_BODY,
    C_WRITING,
    C_FAILED,
    C_CLOSED
};

#define C_HEADER_BYTES 4
#define C_MAX_BODY     64
#define C_SESSIONS     4

typedef struct CSession
{
    char   frame[C_HEADER_BYTES + C_MAX_BODY];
    size_t frame_length;
    int    in_use;
} CSession;

static CSession g_sessions[C_SESSIONS];
static int      g_released;

static uint32_t decodeLength(KiottyNetSpan header)
{
    const unsigned char* p = (const unsigned char*)header.data;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static KiottyNetTransition readHeader(void)
{
    return KiottyNet_moveToNextState(C_READING_HEADER, KiottyNet_thenRead(C_HEADER_BYTES, 5000));
}

static KiottyNetTransition onConnected(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return readHeader();
}

static KiottyNetTransition onHeader(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    CSession* s = (CSession*)session;
    uint32_t length = decodeLength(event.data);
    (void)connection;
    if (length == 0 || length > C_MAX_BODY)
    {
        return KiottyNet_moveToNextState(C_FAILED, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
    }
    memcpy(s->frame, event.data.data, C_HEADER_BYTES);
    s->frame_length = C_HEADER_BYTES + length;
    return KiottyNet_moveToNextState(C_READING_BODY, KiottyNet_thenRead(length, 5000));
}

static KiottyNetTransition onBody(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    CSession* s = (CSession*)session;
    KiottyNetSpan frame;
    (void)connection;
    memcpy(s->frame + C_HEADER_BYTES, event.data.data, event.data.length);
    frame.data = s->frame;
    frame.length = s->frame_length;
    return KiottyNet_moveToNextState(C_WRITING, KiottyNet_thenWrite(frame, 5000));
}

static KiottyNetTransition onWritten(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return readHeader();
}

static KiottyNetTransition onTimeout(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_moveToNextState(C_FAILED, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
}

static KiottyNetTransition onClosed(KiottyNetConnectionContext session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
{
    (void)session;
    (void)connection;
    (void)event;
    return KiottyNet_moveToNextState(C_CLOSED, KiottyNet_thenHold(0));
}

static const KiottyNetRule RULES[] = {
    KIOTTY_NET_RULE(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED, onConnected, C_READING_HEADER),
    KIOTTY_NET_RULE(C_READING_HEADER, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, onHeader, C_READING_BODY, C_FAILED),
    KIOTTY_NET_RULE(C_READING_BODY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, onBody, C_WRITING),
    KIOTTY_NET_RULE(C_WRITING, KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN, onWritten, C_READING_HEADER),
    KIOTTY_NET_RULE_UNCHECKED(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT, onTimeout),
    KIOTTY_NET_RULE_UNCHECKED(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED, onClosed)
};

static const KiottyNetStateMachine MACHINE = { RULES, KIOTTY_NET_RULE_COUNT(RULES), C_READING_HEADER };

static const KiottyNetRule DUPLICATE_RULES[] = {
    KIOTTY_NET_RULE(C_READING_HEADER, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, onHeader, C_READING_BODY),
    KIOTTY_NET_RULE(C_READING_HEADER, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, onBody, C_WRITING)
};

static const KiottyNetStateMachine DUPLICATE_MACHINE = { DUPLICATE_RULES, KIOTTY_NET_RULE_COUNT(DUPLICATE_RULES), C_READING_HEADER };

static KiottyNetConnectionContext acquire(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
{
    size_t i;
    (void)app_context;
    (void)info;
    for (i = 0; i < C_SESSIONS; ++i)
    {
        if (!g_sessions[i].in_use)
        {
            memset(&g_sessions[i], 0, sizeof(g_sessions[i]));
            g_sessions[i].in_use = 1;
            return &g_sessions[i];
        }
    }
    return NULL;
}

static void release(KiottyNetAppContext app_context, KiottyNetConnectionContext session, KiottyNetConnectionHandle connection)
{
    (void)app_context;
    (void)connection;
    ((CSession*)session)->in_use = 0;
    ++g_released;
}

static const KiottyNetStatefulAppContext STATEFUL = { NULL, &MACHINE, acquire, release, NULL };
static const KiottyNetStatefulAppContext DUPLICATE_STATEFUL = { NULL, &DUPLICATE_MACHINE, acquire, release, NULL };

const KiottyNetStatefulAppContext* kiottyTestCEchoServer(void)
{
    memset(g_sessions, 0, sizeof(g_sessions));
    g_released = 0;
    return &STATEFUL;
}

const KiottyNetStatefulAppContext* kiottyTestCDuplicateRules(void)
{
    return &DUPLICATE_STATEFUL;
}

int kiottyTestCEchoReleased(void)
{
    return g_released;
}
