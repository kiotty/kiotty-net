#include "kiotty_net_statemachine.h"

KiottyNetTransition KiottyNet_moveToNextState(int32_t next, KiottyNetConnectionAction entry_action)
{
    KiottyNetTransition transition = {};
    transition.next = next;
    transition.entry_action = entry_action;
    return transition;
}
