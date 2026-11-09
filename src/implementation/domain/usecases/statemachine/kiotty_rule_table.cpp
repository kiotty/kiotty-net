#include "domain/usecases/statemachine/kiotty_rule_table.h"

#include "core/kiotty_span.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t  ANY_STATE = KIOTTY_NET_ANY_STATE;
        constexpr uint32_t MAX_NEXT = KIOTTY_NET_MAX_NEXT;

        using Rules = Span<const KiottyNetRule>;

        bool isRuleValid(const KiottyNetRule& rule);
        bool isEventKnown(KiottyNetConnectionEventType event);
        bool areNextStatesValid(const KiottyNetNextStates& next);
        bool hasDuplicate(Rules rules);
        bool hasKeyAfter(Rules rules, size_t index);
        bool isSameKey(const KiottyNetRule& left, const KiottyNetRule& right);
    }

    RuleResult findRule(const KiottyNetStateMachine& machine,
                        int32_t state,
                        KiottyNetConnectionEventType event)
    {
        const Rules rules{ machine.rules, machine.count };
        const KiottyNetRule* fallback = nullptr;
        for (const KiottyNetRule& rule : rules)
        {
            if (rule.event != event) { continue; }
            if (rule.state == state) { return RuleResult::success(rule); }
            if (rule.state == ANY_STATE) { fallback = &rule; }
        }
        if (fallback == nullptr) { return RuleResult::failure(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
        return RuleResult::success(*fallback);
    }

    bool isAllowed(const KiottyNetRule& rule, int32_t next)
    {
        const KiottyNetNextStates& allowed = rule.next;
        if (allowed.count == 0) { return true; }
        for (uint32_t i = 0; i < allowed.count; ++i)
        {
            if (allowed.at[i] == next) { return true; }
        }
        return false;
    }

    KiottyNetCode checkRules(const KiottyNetStateMachine* machine)
    {
        const bool has_rules = machine != nullptr && machine->rules != nullptr;
        if (!has_rules) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (machine->count == 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Rules rules{ machine->rules, machine->count };
        for (const KiottyNetRule& rule : rules)
        {
            if (!isRuleValid(rule)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        }
        if (hasDuplicate(rules)) { return KIOTTY_NET_CODE_DUPLICATE_RULE; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    namespace
    {
        bool isRuleValid(const KiottyNetRule& rule)
        {
            if (rule.transition_action == nullptr) { return false; }
            if (!isEventKnown(rule.event)) { return false; }
            if (rule.state < ANY_STATE) { return false; }
            if (rule.next.count > MAX_NEXT) { return false; }
            return areNextStatesValid(rule.next);
        }

        bool isEventKnown(KiottyNetConnectionEventType event)
        {
            switch (event)
            {
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_READ:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED:
                return true;
            }
            return false;
        }

        bool areNextStatesValid(const KiottyNetNextStates& next)
        {
            for (uint32_t i = 0; i < next.count; ++i)
            {
                if (next.at[i] < ANY_STATE) { return false; }
            }
            return true;
        }

        bool hasDuplicate(Rules rules)
        {
            for (size_t i = 0; i < rules.length; ++i)
            {
                if (hasKeyAfter(rules, i)) { return true; }
            }
            return false;
        }

        bool hasKeyAfter(Rules rules, size_t index)
        {
            const KiottyNetRule& rule = rules[index];
            for (size_t j = index + 1; j < rules.length; ++j)
            {
                if (isSameKey(rule, rules[j])) { return true; }
            }
            return false;
        }

        bool isSameKey(const KiottyNetRule& left, const KiottyNetRule& right)
        {
            return left.state == right.state && left.event == right.event;
        }
    }
}
