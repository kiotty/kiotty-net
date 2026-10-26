#ifndef KIOTTY_NET_STATEMACHINE_H
#define KIOTTY_NET_STATEMACHINE_H

#include "kiotty_net_base.h"

#ifdef __cplusplus
#include <new>

extern "C" {
#endif

#define KIOTTY_ANY_STATE  (-1)
#define KIOTTY_SAME_STATE (-2)
#define KIOTTY_MAX_NEXT   4

typedef enum KiottyNetConnectionEventType
{
    KIOTTY_CONNECTION_EVENT_CONNECTED,
    KIOTTY_CONNECTION_EVENT_READ,
    KIOTTY_CONNECTION_EVENT_WRITTEN,
    KIOTTY_CONNECTION_EVENT_WORK_STARTED,
    KIOTTY_CONNECTION_EVENT_TIMEOUT,
    KIOTTY_CONNECTION_EVENT_CLOSED
} KiottyNetConnectionEventType;

typedef struct KiottyNetConnectionEvent
{
    KiottyNetConnectionEventType type;
    union
    {
        struct
        {
            KiottyNetSpan data;
        } read;
        struct
        {
            size_t length;
        } written;
        struct
        {
            KiottyNetWorkHandle work;
        } work_started;
        struct
        {
            KiottyNetCode reason;
        } closed;
    } as;
} KiottyNetConnectionEvent;

typedef enum KiottyNetEntryActionType
{
    KIOTTY_ENTRY_ACTION_NONE,
    KIOTTY_ENTRY_ACTION_READ,
    KIOTTY_ENTRY_ACTION_READ_UNTIL,
    KIOTTY_ENTRY_ACTION_WRITE,
    KIOTTY_ENTRY_ACTION_START_WORK,
    KIOTTY_ENTRY_ACTION_CLOSE
} KiottyNetEntryActionType;

typedef struct KiottyNetEntryAction
{
    KiottyNetEntryActionType type;
    uint32_t                 timeout_ms;
    union
    {
        struct
        {
            size_t exact;
        } read;
        struct
        {
            KiottyNetSpan delimiter;
            size_t        max;
        } read_until;
        struct
        {
            KiottyNetSpan data;
        } write;
    } as;
} KiottyNetEntryAction;

typedef struct KiottyNetTransition
{
    int32_t              next;
    KiottyNetEntryAction entry_action;
} KiottyNetTransition;

typedef KiottyNetTransition (*KiottyNetTransitionAction)(void* session, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event);

typedef struct KiottyNetRule
{
    int32_t                      state;
    KiottyNetConnectionEventType event;
    KiottyNetTransitionAction    transition_action;
    int32_t                      next[KIOTTY_MAX_NEXT];
    uint32_t                     next_count;
} KiottyNetRule;

typedef struct KiottyNetStateMachine
{
    const KiottyNetRule* rules;
    size_t               count;
    int32_t              initial;
    size_t               session_size;
} KiottyNetStateMachine;

#define KIOTTY_RULE(state, event, transition_action, ...)      { (state), (event), (transition_action), { __VA_ARGS__ }, (uint32_t)(sizeof((int32_t[]){ __VA_ARGS__ }) / sizeof(int32_t)) }
#define KIOTTY_RULE_UNCHECKED(state, event, transition_action) { (state), (event), (transition_action), { 0 }, 0 }
#define KIOTTY_RULE_COUNT(table)                               (sizeof(table) / sizeof((table)[0]))

KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeNoEntryAction(void);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeReadEntryAction(size_t exact);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeReadUntilEntryAction(KiottyNetSpan delimiter, size_t max);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeWriteEntryAction(KiottyNetSpan data);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeStartWorkEntryAction(void);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_makeCloseEntryAction(void);
KIOTTY_NET_API KiottyNetEntryAction KiottyNet_setEntryActionTimeout(KiottyNetEntryAction entry_action, uint32_t ms);
KIOTTY_NET_API KiottyNetTransition  KiottyNet_moveToNextState(int32_t next, KiottyNetEntryAction entry_action);
KIOTTY_NET_API KiottyNetTransition  KiottyNet_stayInCurrentState(KiottyNetEntryAction entry_action);

typedef struct KiottyNetFsmRunner* KiottyNetFsmRunnerHandle;

#define KIOTTY_FSM_POOL_SIZE(session_size, max_clients) (64 + (max_clients) * (16 + (((session_size) + 15) & ~(size_t)15)))

KIOTTY_NET_API KiottyNetFsmRunnerHandle   KiottyNet_initFsmRunner(const KiottyNetStateMachine* machine, void* pool, size_t max_clients);
KIOTTY_NET_API KiottyNetConnectionHandler KiottyNet_getFsmHandler(KiottyNetFsmRunnerHandle runner);

#ifdef __cplusplus
}

class KiottyNet_EntryAction;

template <typename State>
struct KiottyNet_Transition;

template <typename State, typename Session>
class KiottyNet_Runner;

namespace kiotty
{
    namespace net
    {
        template <typename State, typename Session>
        struct Rule
        {
            typedef KiottyNet_Transition<State> (*TransitionAction)(Session& session, KiottyNet_Connection& connection, const KiottyNetConnectionEvent& event);

            constexpr Rule(int32_t state, KiottyNetConnectionEventType event, TransitionAction transition_action);
            constexpr Rule(int32_t state, KiottyNetConnectionEventType event, TransitionAction transition_action, uint32_t next_count, int32_t n0, int32_t n1, int32_t n2, int32_t n3);

            constexpr Rule to(State a) const;
            constexpr Rule to(State a, State b) const;
            constexpr Rule to(State a, State b, State c) const;
            constexpr Rule to(State a, State b, State c, State d) const;

            int32_t                      state;
            KiottyNetConnectionEventType event;
            TransitionAction             transition_action;
            int32_t                      next[KIOTTY_MAX_NEXT];
            uint32_t                     next_count;
        };

        static_assert(KIOTTY_MAX_NEXT == 4, "Rule::to overloads assume KIOTTY_MAX_NEXT == 4");

        template <typename R, size_t N>
        struct Table
        {
            R rule[N];
        };

        template <typename State>
        struct FromWhen
        {
            constexpr FromWhen(int32_t state, KiottyNetConnectionEventType event);

            template <typename Session>
            constexpr Rule<State, Session> then(KiottyNet_Transition<State> (*transition_action)(Session&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)) const;

            int32_t                      state;
            KiottyNetConnectionEventType event;
        };

        template <typename State>
        struct From
        {
            constexpr explicit From(State state);

            constexpr FromWhen<State> when(KiottyNetConnectionEventType event) const;

            int32_t state;
        };

        struct When
        {
            constexpr explicit When(KiottyNetConnectionEventType event);

            template <typename State, typename Session>
            constexpr Rule<State, Session> then(KiottyNet_Transition<State> (*transition_action)(Session&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)) const;

            KiottyNetConnectionEventType event;
        };

        template <size_t... I>
        struct Seq
        {
        };

        template <size_t N, size_t... I>
        struct MakeSeq : MakeSeq<N - 1, N - 1, I...>
        {
        };

        template <size_t... I>
        struct MakeSeq<0, I...>
        {
            typedef Seq<I...> type;
        };

        template <typename R>
        constexpr bool precedes(const R& a, const R& b);

        template <typename R>
        constexpr bool sameKey(const R& a, const R& b);

        template <typename R, size_t N>
        constexpr size_t insertPosition(const Table<R, N>& table, const R& rule, size_t i);

        template <typename R, size_t N>
        constexpr R pick(const Table<R, N>& table, const R& rule, size_t position, size_t k);

        template <typename R, size_t N, size_t... I>
        constexpr Table<R, N + 1> insertAt(const Table<R, N>& table, const R& rule, size_t position, Seq<I...>);

        template <typename R, size_t N>
        constexpr Table<R, N + 1> insert(const Table<R, N>& table, const R& rule);

        template <typename R, size_t N>
        constexpr bool unique(const Table<R, N>& table, size_t i);

        template <typename R, size_t N>
        Table<R, N> duplicate_rule_in_table();

        template <typename R, size_t N>
        constexpr Table<R, N> checked(const Table<R, N>& table);

        template <typename R>
        constexpr Table<R, 1> sorted(R rule);

        template <typename R, typename... Rs>
        constexpr Table<R, 1 + sizeof...(Rs)> sorted(R first, Rs... rest);

        template <typename State, typename Session>
        struct Slot
        {
            Slot();

            KiottyNet_Runner<State, Session>* runner;
            State                             state;
            bool                              in_use;
            Session                           session;
        };

        inline KiottyNetConnectionEvent makeEvent(KiottyNetConnectionEventType type);
    }
}

class KiottyNet_EntryAction
{
public:
    KiottyNet_EntryAction();

    static KiottyNet_EntryAction read(size_t exact);
    static KiottyNet_EntryAction readUntil(KiottyNetSpan delimiter, size_t max);
    static KiottyNet_EntryAction write(KiottyNetSpan data);
    static KiottyNet_EntryAction startWork();
    static KiottyNet_EntryAction close();

    KiottyNet_EntryAction& timeout(uint32_t ms);

    operator KiottyNetEntryAction() const;

private:
    explicit KiottyNet_EntryAction(KiottyNetEntryAction raw);

    KiottyNetEntryAction _raw;
};

template <typename State>
struct KiottyNet_Transition
{
    static KiottyNet_Transition moveToNextState(State next, KiottyNetEntryAction entry_action = KiottyNetEntryAction());
    static KiottyNet_Transition stayInCurrentState(KiottyNetEntryAction entry_action = KiottyNetEntryAction());

    int32_t              next;
    KiottyNetEntryAction entry_action;

private:
    KiottyNet_Transition(int32_t next, KiottyNetEntryAction entry_action);
};

template <typename State>
constexpr kiotty::net::From<State> KiottyNet_from(State state);

constexpr kiotty::net::When KiottyNet_when(KiottyNetConnectionEventType event);

template <typename R, typename... Rs>
constexpr kiotty::net::Table<R, 1 + sizeof...(Rs)> KiottyNet_rules(R first, Rs... rest);

template <typename State, typename Session, size_t N>
struct KiottyNet_Slots
{
    static_assert(N > 0, "KiottyNet_Slots needs at least one slot");

    kiotty::net::Slot<State, Session> slot[N];
};

template <typename State, typename Session>
class KiottyNet_Runner
{
public:
    typedef kiotty::net::Rule<State, Session> Rule;

    template <size_t N, size_t M>
    KiottyNet_Runner(const kiotty::net::Table<Rule, N>& table, State initial, KiottyNet_Slots<State, Session, M>& slots);

    template <size_t N, size_t M>
    KiottyNet_Runner(kiotty::net::Table<Rule, N>&& table, State initial, KiottyNet_Slots<State, Session, M>& slots) = delete;

    KiottyNet_Runner(const KiottyNet_Runner&) = delete;
    KiottyNet_Runner& operator=(const KiottyNet_Runner&) = delete;

    KiottyNetConnectionHandler handler();

private:
    typedef kiotty::net::Slot<State, Session> Slot;

    static KiottyNet_Runner& self(void* handler_context);
    static Slot*             slotOf(void* connection_context);
    static void              dispatch(void* connection_context, KiottyNetConnectionHandle connection, const KiottyNetConnectionEvent& event);

    Slot*       acquire();
    const Rule* find(int32_t state, KiottyNetConnectionEventType event) const;
    void        deliver(Slot& slot, KiottyNetConnectionHandle connection, const KiottyNetConnectionEvent& event);

    static bool          allowed(const Rule& rule, const KiottyNet_Transition<State>& transition);
    static void          fail(KiottyNetConnectionHandle connection, const KiottyNetConnectionEvent& event, KiottyNetCode code);
    static void          apply(KiottyNet_Connection& connection, const KiottyNetEntryAction& entry_action);
    static void          applyToWork(KiottyNetWorkHandle work, const KiottyNetEntryAction& entry_action);
    static KiottyNetCode finishWork(KiottyNetWorkHandle work, const KiottyNetEntryAction& entry_action);

    static void onConnected(void* handler_context, KiottyNetConnectionHandle connection);
    static void onDisconnected(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetCode reason);
    static void onRead(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetSpan data);
    static void onWritten(void* connection_context, KiottyNetConnectionHandle connection, size_t length);
    static void onWorkStarted(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetWorkHandle work);
    static void onTimeout(void* connection_context, KiottyNetConnectionHandle connection);

    const Rule* _rule;
    size_t      _count;
    State       _initial;
    Slot*       _slot;
    size_t      _capacity;
};

namespace kiotty
{
    namespace net
    {
        template <typename State, typename Session>
        constexpr Rule<State, Session>::Rule(int32_t state_, KiottyNetConnectionEventType event_, TransitionAction transition_action_)
            : state(state_)
            , event(event_)
            , transition_action(transition_action_)
            , next{ 0, 0, 0, 0 }
            , next_count(0)
        {
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session>::Rule(int32_t state_, KiottyNetConnectionEventType event_, TransitionAction transition_action_, uint32_t next_count_, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
            : state(state_)
            , event(event_)
            , transition_action(transition_action_)
            , next{ n0, n1, n2, n3 }
            , next_count(next_count_)
        {
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session> Rule<State, Session>::to(State a) const
        {
            return Rule(state, event, transition_action, 1, static_cast<int32_t>(a), 0, 0, 0);
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session> Rule<State, Session>::to(State a, State b) const
        {
            return Rule(state, event, transition_action, 2, static_cast<int32_t>(a), static_cast<int32_t>(b), 0, 0);
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session> Rule<State, Session>::to(State a, State b, State c) const
        {
            return Rule(state, event, transition_action, 3, static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c), 0);
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session> Rule<State, Session>::to(State a, State b, State c, State d) const
        {
            return Rule(state, event, transition_action, 4, static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c), static_cast<int32_t>(d));
        }

        template <typename State>
        constexpr FromWhen<State>::FromWhen(int32_t state_, KiottyNetConnectionEventType event_)
            : state(state_)
            , event(event_)
        {
        }

        template <typename State>
        template <typename Session>
        constexpr Rule<State, Session> FromWhen<State>::then(KiottyNet_Transition<State> (*transition_action)(Session&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)) const
        {
            return Rule<State, Session>(state, event, transition_action);
        }

        template <typename State>
        constexpr From<State>::From(State state_)
            : state(static_cast<int32_t>(state_))
        {
        }

        template <typename State>
        constexpr FromWhen<State> From<State>::when(KiottyNetConnectionEventType event_) const
        {
            return FromWhen<State>(state, event_);
        }

        constexpr When::When(KiottyNetConnectionEventType event_)
            : event(event_)
        {
        }

        template <typename State, typename Session>
        constexpr Rule<State, Session> When::then(KiottyNet_Transition<State> (*transition_action)(Session&, KiottyNet_Connection&, const KiottyNetConnectionEvent&)) const
        {
            return Rule<State, Session>(KIOTTY_ANY_STATE, event, transition_action);
        }

        template <typename R>
        constexpr bool precedes(const R& a, const R& b)
        {
            return a.state < b.state || (a.state == b.state && a.event < b.event);
        }

        template <typename R>
        constexpr bool sameKey(const R& a, const R& b)
        {
            return a.state == b.state && a.event == b.event;
        }

        template <typename R, size_t N>
        constexpr size_t insertPosition(const Table<R, N>& table, const R& rule, size_t i)
        {
            return i == N ? N : (precedes(rule, table.rule[i]) ? i : insertPosition(table, rule, i + 1));
        }

        template <typename R, size_t N>
        constexpr R pick(const Table<R, N>& table, const R& rule, size_t position, size_t k)
        {
            return k < position ? table.rule[k] : (k == position ? rule : table.rule[k - 1]);
        }

        template <typename R, size_t N, size_t... I>
        constexpr Table<R, N + 1> insertAt(const Table<R, N>& table, const R& rule, size_t position, Seq<I...>)
        {
            return Table<R, N + 1>{ { pick(table, rule, position, I)... } };
        }

        template <typename R, size_t N>
        constexpr Table<R, N + 1> insert(const Table<R, N>& table, const R& rule)
        {
            return insertAt(table, rule, insertPosition(table, rule, 0), typename MakeSeq<N + 1>::type());
        }

        template <typename R, size_t N>
        constexpr bool unique(const Table<R, N>& table, size_t i)
        {
            return i + 1 >= N ? true : (!sameKey(table.rule[i], table.rule[i + 1]) && unique(table, i + 1));
        }

        template <typename R, size_t N>
        constexpr Table<R, N> checked(const Table<R, N>& table)
        {
            return unique(table, 0) ? table : duplicate_rule_in_table<R, N>();
        }

        template <typename R>
        constexpr Table<R, 1> sorted(R rule)
        {
            return Table<R, 1>{ { rule } };
        }

        template <typename R, typename... Rs>
        constexpr Table<R, 1 + sizeof...(Rs)> sorted(R first, Rs... rest)
        {
            return insert(sorted(rest...), first);
        }

        template <typename State, typename Session>
        inline Slot<State, Session>::Slot()
            : runner(nullptr)
            , state()
            , in_use(false)
            , session()
        {
        }

        inline KiottyNetConnectionEvent makeEvent(KiottyNetConnectionEventType type)
        {
            KiottyNetConnectionEvent event = {};
            event.type = type;
            return event;
        }
    }
}

inline KiottyNet_EntryAction::KiottyNet_EntryAction()
    : _raw(KiottyNet_makeNoEntryAction())
{
}

inline KiottyNet_EntryAction::KiottyNet_EntryAction(KiottyNetEntryAction raw)
    : _raw(raw)
{
}

inline KiottyNet_EntryAction KiottyNet_EntryAction::read(size_t exact)
{
    return KiottyNet_EntryAction(KiottyNet_makeReadEntryAction(exact));
}

inline KiottyNet_EntryAction KiottyNet_EntryAction::readUntil(KiottyNetSpan delimiter, size_t max)
{
    return KiottyNet_EntryAction(KiottyNet_makeReadUntilEntryAction(delimiter, max));
}

inline KiottyNet_EntryAction KiottyNet_EntryAction::write(KiottyNetSpan data)
{
    return KiottyNet_EntryAction(KiottyNet_makeWriteEntryAction(data));
}

inline KiottyNet_EntryAction KiottyNet_EntryAction::startWork()
{
    return KiottyNet_EntryAction(KiottyNet_makeStartWorkEntryAction());
}

inline KiottyNet_EntryAction KiottyNet_EntryAction::close()
{
    return KiottyNet_EntryAction(KiottyNet_makeCloseEntryAction());
}

inline KiottyNet_EntryAction& KiottyNet_EntryAction::timeout(uint32_t ms)
{
    _raw = KiottyNet_setEntryActionTimeout(_raw, ms);
    return *this;
}

inline KiottyNet_EntryAction::operator KiottyNetEntryAction() const
{
    return _raw;
}

template <typename State>
inline KiottyNet_Transition<State>::KiottyNet_Transition(int32_t next_, KiottyNetEntryAction entry_action_)
    : next(next_)
    , entry_action(entry_action_)
{
}

template <typename State>
inline KiottyNet_Transition<State> KiottyNet_Transition<State>::moveToNextState(State next, KiottyNetEntryAction entry_action)
{
    return KiottyNet_Transition(static_cast<int32_t>(next), entry_action);
}

template <typename State>
inline KiottyNet_Transition<State> KiottyNet_Transition<State>::stayInCurrentState(KiottyNetEntryAction entry_action)
{
    return KiottyNet_Transition(static_cast<int32_t>(KIOTTY_SAME_STATE), entry_action);
}

template <typename State>
constexpr kiotty::net::From<State> KiottyNet_from(State state)
{
    return kiotty::net::From<State>(state);
}

constexpr kiotty::net::When KiottyNet_when(KiottyNetConnectionEventType event)
{
    return kiotty::net::When(event);
}

template <typename R, typename... Rs>
constexpr kiotty::net::Table<R, 1 + sizeof...(Rs)> KiottyNet_rules(R first, Rs... rest)
{
    return kiotty::net::checked(kiotty::net::sorted(first, rest...));
}

template <typename State, typename Session>
template <size_t N, size_t M>
inline KiottyNet_Runner<State, Session>::KiottyNet_Runner(const kiotty::net::Table<Rule, N>& table, State initial, KiottyNet_Slots<State, Session, M>& slots)
    : _rule(table.rule)
    , _count(N)
    , _initial(initial)
    , _slot(slots.slot)
    , _capacity(M)
{
}

template <typename State, typename Session>
inline KiottyNetConnectionHandler KiottyNet_Runner<State, Session>::handler()
{
    KiottyNetConnectionHandler result = { this, &onConnected, &onDisconnected, &onRead, &onWritten, &onWorkStarted, &onTimeout };
    return result;
}

template <typename State, typename Session>
inline KiottyNet_Runner<State, Session>& KiottyNet_Runner<State, Session>::self(void* handler_context)
{
    return *static_cast<KiottyNet_Runner*>(handler_context);
}

template <typename State, typename Session>
inline typename KiottyNet_Runner<State, Session>::Slot* KiottyNet_Runner<State, Session>::slotOf(void* connection_context)
{
    return static_cast<Slot*>(connection_context);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::dispatch(void* connection_context, KiottyNetConnectionHandle connection, const KiottyNetConnectionEvent& event)
{
    Slot* slot = slotOf(connection_context);
    if (slot == nullptr || slot->runner == nullptr)
    {
        return;
    }
    slot->runner->deliver(*slot, connection, event);
}

template <typename State, typename Session>
inline typename KiottyNet_Runner<State, Session>::Slot* KiottyNet_Runner<State, Session>::acquire()
{
    for (size_t i = 0; i < _capacity; ++i)
    {
        if (_slot[i].in_use)
        {
            continue;
        }
        _slot[i].runner = this;
        _slot[i].in_use = true;
        _slot[i].state = _initial;
        _slot[i].session.~Session();
        new (&_slot[i].session) Session();
        return &_slot[i];
    }
    return nullptr;
}

template <typename State, typename Session>
inline const typename KiottyNet_Runner<State, Session>::Rule* KiottyNet_Runner<State, Session>::find(int32_t state, KiottyNetConnectionEventType event) const
{
    size_t low = 0;
    size_t high = _count;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        const Rule& rule = _rule[middle];
        if (rule.state == state && rule.event == event)
        {
            return &rule;
        }
        if (rule.state < state || (rule.state == state && rule.event < event))
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return nullptr;
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::deliver(Slot& slot, KiottyNetConnectionHandle handle, const KiottyNetConnectionEvent& event)
{
    const Rule* rule = find(static_cast<int32_t>(slot.state), event.type);
    if (rule == nullptr)
    {
        rule = find(KIOTTY_ANY_STATE, event.type);
    }
    if (rule == nullptr)
    {
        fail(handle, event, KIOTTY_UNHANDLED_EVENT);
        return;
    }
    KiottyNet_Connection connection(handle);
    KiottyNet_Transition<State> transition = rule->transition_action(slot.session, connection, event);
    if (event.type == KIOTTY_CONNECTION_EVENT_CLOSED)
    {
        return;
    }
#ifndef NDEBUG
    if (!allowed(*rule, transition))
    {
        fail(handle, event, KIOTTY_ILLEGAL_TRANSITION);
        return;
    }
#endif
    if (transition.next != KIOTTY_SAME_STATE)
    {
        slot.state = static_cast<State>(transition.next);
    }
    if (event.type == KIOTTY_CONNECTION_EVENT_WORK_STARTED)
    {
        applyToWork(event.as.work_started.work, transition.entry_action);
        return;
    }
    apply(connection, transition.entry_action);
}

template <typename State, typename Session>
inline bool KiottyNet_Runner<State, Session>::allowed(const Rule& rule, const KiottyNet_Transition<State>& transition)
{
    if (transition.next == KIOTTY_SAME_STATE || rule.next_count == 0)
    {
        return true;
    }
    for (uint32_t i = 0; i < rule.next_count && i < KIOTTY_MAX_NEXT; ++i)
    {
        if (rule.next[i] == transition.next)
        {
            return true;
        }
    }
    return false;
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::fail(KiottyNetConnectionHandle connection, const KiottyNetConnectionEvent& event, KiottyNetCode code)
{
    if (event.type == KIOTTY_CONNECTION_EVENT_CLOSED)
    {
        return;
    }
    if (event.type == KIOTTY_CONNECTION_EVENT_WORK_STARTED)
    {
        KiottyNet_finishWorkWithClose(event.as.work_started.work, code);
        return;
    }
    KiottyNet_closeConnection(connection, code);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::apply(KiottyNet_Connection& connection, const KiottyNetEntryAction& entry_action)
{
    KiottyNetCode code = KIOTTY_SUCCESS;
    switch (entry_action.type)
    {
    case KIOTTY_ENTRY_ACTION_NONE:
        return;
    case KIOTTY_ENTRY_ACTION_READ:
        connection.read(entry_action.as.read.exact);
        break;
    case KIOTTY_ENTRY_ACTION_READ_UNTIL:
        connection.readUntil(entry_action.as.read_until.delimiter, entry_action.as.read_until.max);
        break;
    case KIOTTY_ENTRY_ACTION_WRITE:
        connection.write(entry_action.as.write.data);
        break;
    case KIOTTY_ENTRY_ACTION_START_WORK:
        code = connection.startWork();
        break;
    case KIOTTY_ENTRY_ACTION_CLOSE:
        connection.close(KIOTTY_SUCCESS);
        return;
    default:
        connection.close(KIOTTY_INVALID_ARGUMENT);
        return;
    }
    if (code != KIOTTY_SUCCESS)
    {
        connection.close(code);
        return;
    }
    connection.setTimeout(entry_action.timeout_ms);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::applyToWork(KiottyNetWorkHandle work, const KiottyNetEntryAction& entry_action)
{
    KiottyNetCode code = finishWork(work, entry_action);
    if (code != KIOTTY_SUCCESS)
    {
        KiottyNet_finishWorkWithClose(work, code);
    }
}

template <typename State, typename Session>
inline KiottyNetCode KiottyNet_Runner<State, Session>::finishWork(KiottyNetWorkHandle work, const KiottyNetEntryAction& entry_action)
{
    switch (entry_action.type)
    {
    case KIOTTY_ENTRY_ACTION_NONE:
        return KIOTTY_SUCCESS;
    case KIOTTY_ENTRY_ACTION_READ:
        return KiottyNet_finishWorkWithRead(work, entry_action.as.read.exact, entry_action.timeout_ms);
    case KIOTTY_ENTRY_ACTION_READ_UNTIL:
        return KiottyNet_finishWorkWithReadUntil(work, entry_action.as.read_until.delimiter, entry_action.as.read_until.max, entry_action.timeout_ms);
    case KIOTTY_ENTRY_ACTION_WRITE:
        return KiottyNet_finishWorkWithWrite(work, entry_action.as.write.data);
    case KIOTTY_ENTRY_ACTION_CLOSE:
        return KiottyNet_finishWorkWithClose(work, KIOTTY_SUCCESS);
    default:
        return KIOTTY_INVALID_ARGUMENT;
    }
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onConnected(void* handler_context, KiottyNetConnectionHandle connection)
{
    Slot* slot = self(handler_context).acquire();
    if (slot == nullptr)
    {
        KiottyNet_closeConnection(connection, KIOTTY_SESSION_EXHAUSTED);
        return;
    }
    KiottyNet_setConnectionContext(connection, slot);
    self(handler_context).deliver(*slot, connection, kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_CONNECTED));
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onDisconnected(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
{
    KiottyNetConnectionEvent event = kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_CLOSED);
    event.as.closed.reason = reason;
    dispatch(connection_context, connection, event);
    Slot* slot = slotOf(connection_context);
    if (slot == nullptr)
    {
        return;
    }
    slot->in_use = false;
    KiottyNet_setConnectionContext(connection, nullptr);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onRead(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
{
    KiottyNetConnectionEvent event = kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_READ);
    event.as.read.data = data;
    dispatch(connection_context, connection, event);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onWritten(void* connection_context, KiottyNetConnectionHandle connection, size_t length)
{
    KiottyNetConnectionEvent event = kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_WRITTEN);
    event.as.written.length = length;
    dispatch(connection_context, connection, event);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onWorkStarted(void* connection_context, KiottyNetConnectionHandle connection, KiottyNetWorkHandle work)
{
    KiottyNetConnectionEvent event = kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_WORK_STARTED);
    event.as.work_started.work = work;
    dispatch(connection_context, connection, event);
}

template <typename State, typename Session>
inline void KiottyNet_Runner<State, Session>::onTimeout(void* connection_context, KiottyNetConnectionHandle connection)
{
    dispatch(connection_context, connection, kiotty::net::makeEvent(KIOTTY_CONNECTION_EVENT_TIMEOUT));
}

#endif

#endif
