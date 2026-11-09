#if !defined(KIOTTY_TEST_STATEFUL_C_H)
#define KIOTTY_TEST_STATEFUL_C_H

#include "kiotty_net_statemachine.h"

#ifdef __cplusplus
extern "C" {
#endif

const KiottyNetStatefulAppContext* kiottyTestCEchoServer(void);
const KiottyNetStatefulAppContext* kiottyTestCDuplicateRules(void);
int                                kiottyTestCEchoReleased(void);

#ifdef __cplusplus
}
#endif

#endif
