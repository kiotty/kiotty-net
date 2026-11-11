#ifndef KIOTTY_NET_STATEMACHINE_H
#define KIOTTY_NET_STATEMACHINE_H

#include "kiotty_net_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_NET_ANY_STATE  (-1)
#define KIOTTY_NET_MAX_NEXT   4

typedef enum KiottyNetConnectionEventType
{
    KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED,
    KIOTTY_NET_CONNECTION_EVENT_TYPE_READ,
    KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN,
    KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED,
    KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT,
    KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED
} KiottyNetConnectionEventType;

typedef struct KiottyNetConnectionEvent
{
    KiottyNetConnectionEventType type;
    KiottyNetSpan                data;
    size_t                       length;
    KiottyNetConnectionStream    stream;
    KiottyNetCode                reason;
} KiottyNetConnectionEvent;

typedef struct KiottyNetTransition
{
    int32_t                   next;
    KiottyNetConnectionAction entry_action;
} KiottyNetTransition;

typedef KiottyNetTransition (*KiottyNetTransitionAction)(
    KiottyNetConnectionContext connection_context,
    KiottyNetConnectionHandle connection,
    KiottyNetConnectionEvent event);

typedef struct KiottyNetNextStates
{
    int32_t  at[KIOTTY_NET_MAX_NEXT];
    uint32_t count;
} KiottyNetNextStates;

typedef struct KiottyNetRule
{
    int32_t                      state;
    KiottyNetConnectionEventType event;
    KiottyNetTransitionAction    transition_action;
    KiottyNetNextStates          next;
} KiottyNetRule;

typedef struct KiottyNetStateMachine
{
    const KiottyNetRule* rules;
    size_t               count;
    int32_t              initial;
} KiottyNetStateMachine;

#if !defined(__cplusplus)
#define KIOTTY_NET_RULE(state, event, transition_action, ...) \
    { (state), (event), (transition_action), { { __VA_ARGS__ }, \
      (uint32_t)(sizeof((int32_t[]){ __VA_ARGS__ }) / sizeof(int32_t)) } }
#endif
#define KIOTTY_NET_RULE_UNCHECKED(state, event, transition_action) \
    { (state), (event), (transition_action), { { 0 }, 0 } }
#define KIOTTY_NET_RULE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

KIOTTY_NET_API KiottyNetTransition KiottyNet_moveToNextState(
    int32_t next,
    KiottyNetConnectionAction entry_action);

typedef struct KiottyNetStatefulAppContext
{
    KiottyNetAppContext          app_context;
    const KiottyNetStateMachine* machine;
    KiottyNetConnectionContext (*createConnectionContext)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionInfo info);
    void (*releaseConnectionContext)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection);
    void (*onError)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionInfo info,
        KiottyNetCode code,
        const char* message);
} KiottyNetStatefulAppContext;

KIOTTY_NET_API KiottyNetCode KiottyNet_runStatefulServer(
    KiottyNetServerHandle server,
    const KiottyNetStatefulAppContext* app_context);
KIOTTY_NET_API KiottyNetCode KiottyNet_runStatefulEventClient(
    KiottyNetEventClientHandle client,
    const KiottyNetStatefulAppContext* app_context);

#ifdef __cplusplus
}

namespace kiotty
{
    namespace net
    {
        struct Rule
        {
            constexpr Rule(
                int32_t state,
                KiottyNetConnectionEventType event,
                KiottyNetTransitionAction transition_action);
            constexpr Rule(
                int32_t state,
                KiottyNetConnectionEventType event,
                KiottyNetTransitionAction transition_action,
                KiottyNetNextStates next);

            template <typename State>
            constexpr Rule to(State a) const;
            template <typename State>
            constexpr Rule to(State a, State b) const;
            template <typename State>
            constexpr Rule to(State a, State b, State c) const;
            template <typename State>
            constexpr Rule to(State a, State b, State c, State d) const;

            KiottyNetRule raw;
        };

        static_assert(
            KIOTTY_NET_MAX_NEXT == 4,
            "Rule::to overloads assume KIOTTY_NET_MAX_NEXT == 4");

        template <size_t N>
        struct Table
        {
            KiottyNetRule rule[N];
        };

        struct FromWhen
        {
            constexpr FromWhen(int32_t state, KiottyNetConnectionEventType event);

            constexpr Rule then(KiottyNetTransitionAction transition_action) const;

            int32_t                      state;
            KiottyNetConnectionEventType event;
        };

        struct From
        {
            constexpr explicit From(int32_t state);

            constexpr FromWhen when(KiottyNetConnectionEventType event) const;

            int32_t state;
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

        constexpr bool precedes(const KiottyNetRule& a, const KiottyNetRule& b);
        constexpr bool sameKey(const KiottyNetRule& a, const KiottyNetRule& b);

        template <size_t N>
        constexpr size_t insertPosition(const Table<N>& table, const KiottyNetRule& rule, size_t i);

        template <size_t N>
        constexpr KiottyNetRule pick(
            const Table<N>& table,
            const KiottyNetRule& rule,
            size_t position,
            size_t k);

        template <size_t N, size_t... I>
        constexpr Table<N + 1> insertAt(
            const Table<N>& table,
            const KiottyNetRule& rule,
            size_t position,
            Seq<I...>);

        template <size_t N>
        constexpr Table<N + 1> insert(const Table<N>& table, const KiottyNetRule& rule);

        template <size_t N>
        constexpr bool unique(const Table<N>& table, size_t i);

        template <size_t N>
        Table<N> duplicate_rule_in_table();

        template <size_t N>
        constexpr Table<N> checked(const Table<N>& table);

        constexpr Table<1> sorted(const Rule& rule);

        template <typename... Rs>
        constexpr Table<1 + sizeof...(Rs)> sorted(const Rule& first, Rs... rest);
    }
}

template <typename State>
struct KiottyNet_Transition
{
    static KiottyNetTransition moveToNextState(State next, KiottyNetConnectionAction entry_action);
};

template <typename State>
constexpr kiotty::net::From KiottyNet_from(State state);

constexpr kiotty::net::FromWhen KiottyNet_when(KiottyNetConnectionEventType event);

template <typename... Rs>
constexpr kiotty::net::Table<1 + sizeof...(Rs)> KiottyNet_rules(
    const kiotty::net::Rule& first,
    Rs... rest);

template <size_t N, typename State>
constexpr KiottyNetStateMachine KiottyNet_makeStateMachine(
    const kiotty::net::Table<N>& table,
    State initial);

namespace kiotty
{
    namespace net
    {
        constexpr Rule::Rule(
            int32_t state,
            KiottyNetConnectionEventType event,
            KiottyNetTransitionAction transition_action)
            : raw{ state, event, transition_action, { { 0, 0, 0, 0 }, 0 } }
        {
        }

        constexpr Rule::Rule(
            int32_t state,
            KiottyNetConnectionEventType event,
            KiottyNetTransitionAction transition_action,
            KiottyNetNextStates next)
            : raw{ state, event, transition_action, next }
        {
        }

        template <typename State>
        constexpr Rule Rule::to(State a) const
        {
            const KiottyNetNextStates next = { { static_cast<int32_t>(a), 0, 0, 0 }, 1 };
            return Rule(raw.state, raw.event, raw.transition_action, next);
        }

        template <typename State>
        constexpr Rule Rule::to(State a, State b) const
        {
            const KiottyNetNextStates next = { { static_cast<int32_t>(a), static_cast<int32_t>(b), 0, 0 }, 2 };
            return Rule(raw.state, raw.event, raw.transition_action, next);
        }

        template <typename State>
        constexpr Rule Rule::to(State a, State b, State c) const
        {
            const KiottyNetNextStates next = { { static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c), 0 }, 3 };
            return Rule(raw.state, raw.event, raw.transition_action, next);
        }

        template <typename State>
        constexpr Rule Rule::to(State a, State b, State c, State d) const
        {
            const KiottyNetNextStates next = { { static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c), static_cast<int32_t>(d) }, 4 };
            return Rule(raw.state, raw.event, raw.transition_action, next);
        }

        constexpr FromWhen::FromWhen(int32_t state_, KiottyNetConnectionEventType event_)
            : state(state_)
            , event(event_)
        {
        }

        constexpr Rule FromWhen::then(KiottyNetTransitionAction transition_action) const
        {
            return Rule(state, event, transition_action);
        }

        constexpr From::From(int32_t state_)
            : state(state_)
        {
        }

        constexpr FromWhen From::when(KiottyNetConnectionEventType event_) const
        {
            return FromWhen(state, event_);
        }

        constexpr bool precedes(const KiottyNetRule& a, const KiottyNetRule& b)
        {
            return a.state < b.state || (a.state == b.state && a.event < b.event);
        }

        constexpr bool sameKey(const KiottyNetRule& a, const KiottyNetRule& b)
        {
            return a.state == b.state && a.event == b.event;
        }

        template <size_t N>
        constexpr size_t insertPosition(const Table<N>& table, const KiottyNetRule& rule, size_t i)
        {
            return i == N ? N : (
                precedes(rule, table.rule[i]) ? i : insertPosition(table, rule, i + 1));
        }

        template <size_t N>
        constexpr KiottyNetRule pick(
            const Table<N>& table,
            const KiottyNetRule& rule,
            size_t position,
            size_t k)
        {
            return k < position ? table.rule[k] : (k == position ? rule : table.rule[k - 1]);
        }

        template <size_t N, size_t... I>
        constexpr Table<N + 1> insertAt(
            const Table<N>& table,
            const KiottyNetRule& rule,
            size_t position,
            Seq<I...>)
        {
            return Table<N + 1>{ { pick(table, rule, position, I)... } };
        }

        template <size_t N>
        constexpr Table<N + 1> insert(const Table<N>& table, const KiottyNetRule& rule)
        {
            return insertAt(
                table,
                rule,
                insertPosition(table, rule, 0),
                typename MakeSeq<N + 1>::type());
        }

        template <size_t N>
        constexpr bool unique(const Table<N>& table, size_t i)
        {
            return i + 1 >= N ? true : (
                !sameKey(table.rule[i], table.rule[i + 1]) && unique(table, i + 1));
        }

        template <size_t N>
        constexpr Table<N> checked(const Table<N>& table)
        {
            return unique(table, 0) ? table : duplicate_rule_in_table<N>();
        }

        constexpr Table<1> sorted(const Rule& rule)
        {
            return Table<1>{ { rule.raw } };
        }

        template <typename... Rs>
        constexpr Table<1 + sizeof...(Rs)> sorted(const Rule& first, Rs... rest)
        {
            return insert(sorted(rest...), first.raw);
        }
    }
}

template <typename State>
inline KiottyNetTransition KiottyNet_Transition<State>::moveToNextState(
    State next,
    KiottyNetConnectionAction entry_action)
{
    return KiottyNet_moveToNextState(static_cast<int32_t>(next), entry_action);
}

template <typename State>
constexpr kiotty::net::From KiottyNet_from(State state)
{
    return kiotty::net::From(static_cast<int32_t>(state));
}

constexpr kiotty::net::FromWhen KiottyNet_when(KiottyNetConnectionEventType event)
{
    return kiotty::net::FromWhen(KIOTTY_NET_ANY_STATE, event);
}

template <typename... Rs>
constexpr kiotty::net::Table<1 + sizeof...(Rs)> KiottyNet_rules(
    const kiotty::net::Rule& first,
    Rs... rest)
{
    return kiotty::net::checked(kiotty::net::sorted(first, rest...));
}

template <size_t N, typename State>
constexpr KiottyNetStateMachine KiottyNet_makeStateMachine(
    const kiotty::net::Table<N>& table,
    State initial)
{
    return KiottyNetStateMachine{ table.rule, N, static_cast<int32_t>(initial) };
}

#endif

#endif
