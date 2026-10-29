#include "kiotty_net_statemachine.h"

KiottyNetEntryAction KiottyNet_makeNoEntryAction(void)
{
    KiottyNetEntryAction entry_action = {};
    entry_action.type = KIOTTY_ENTRY_ACTION_NONE;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_makeReadEntryAction(size_t exact)
{
    KiottyNetEntryAction entry_action = KiottyNet_makeNoEntryAction();
    entry_action.type = KIOTTY_ENTRY_ACTION_READ;
    entry_action.as.read.exact = exact;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_makeReadUntilEntryAction(KiottyNetSpan delimiter, size_t max)
{
    KiottyNetEntryAction entry_action = KiottyNet_makeNoEntryAction();
    entry_action.type = KIOTTY_ENTRY_ACTION_READ_UNTIL;
    entry_action.as.read_until.delimiter = delimiter;
    entry_action.as.read_until.max = max;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_makeWriteEntryAction(KiottyNetSpan data)
{
    KiottyNetEntryAction entry_action = KiottyNet_makeNoEntryAction();
    entry_action.type = KIOTTY_ENTRY_ACTION_WRITE;
    entry_action.as.write.data = data;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_makeStartWorkEntryAction(void)
{
    KiottyNetEntryAction entry_action = KiottyNet_makeNoEntryAction();
    entry_action.type = KIOTTY_ENTRY_ACTION_START_WORK;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_makeCloseEntryAction(KiottyNetCode reason)
{
    KiottyNetEntryAction entry_action = KiottyNet_makeNoEntryAction();
    entry_action.type = KIOTTY_ENTRY_ACTION_CLOSE;
    entry_action.as.close.reason = reason;
    return entry_action;
}

KiottyNetEntryAction KiottyNet_setEntryActionTimeout(KiottyNetEntryAction entry_action, uint32_t ms)
{
    entry_action.timeout_ms = ms;
    return entry_action;
}

KiottyNetTransition KiottyNet_moveToNextState(int32_t next, KiottyNetEntryAction entry_action)
{
    KiottyNetTransition transition = {};
    transition.next = next;
    transition.entry_action = entry_action;
    return transition;
}
