#include "domain/usecases/statemachine/kiotty_rule_table.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

namespace net = kiotty::net::implementation;

namespace
{
    const int32_t IDLE = 0;
    const int32_t BUSY = 1;
    const int32_t DONE = 2;

    KiottyNetTransition stay(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return KiottyNet_moveToNextState(IDLE, KiottyNet_thenHold(0));
    }

    KiottyNetTransition other(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return KiottyNet_moveToNextState(BUSY, KiottyNet_thenHold(0));
    }

    KiottyNetRule ruleOf(int32_t state, KiottyNetConnectionEventType event, KiottyNetTransitionAction action, std::initializer_list<int32_t> next = {})
    {
        KiottyNetRule rule{};
        rule.state = state;
        rule.event = event;
        rule.transition_action = action;
        for (int32_t value : next)
        {
            rule.next.at[rule.next.count] = value;
            ++rule.next.count;
        }
        return rule;
    }

    KiottyNetStateMachine machineOf(const std::vector<KiottyNetRule>& rules)
    {
        return KiottyNetStateMachine{ rules.data(), rules.size(), IDLE };
    }
}

TEST(RuleTable, FindRulePrefersExactStateOverAny)
{
    const std::vector<KiottyNetRule> rules = {
        ruleOf(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay),
        ruleOf(BUSY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, other),
    };
    const KiottyNetStateMachine machine = machineOf(rules);
    const net::RuleResult found = net::findRule(machine, BUSY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ);
    ASSERT_TRUE(found);
    EXPECT_EQ(&rules[1], &found.value());
}

TEST(RuleTable, FindRuleFallsBackToAny)
{
    const std::vector<KiottyNetRule> rules = {
        ruleOf(BUSY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, other),
        ruleOf(KIOTTY_NET_ANY_STATE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay),
    };
    const KiottyNetStateMachine machine = machineOf(rules);
    const net::RuleResult found = net::findRule(machine, IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ);
    ASSERT_TRUE(found);
    EXPECT_EQ(&rules[1], &found.value());
}

TEST(RuleTable, FindRuleWithoutARowIsUnhandledEvent)
{
    const std::vector<KiottyNetRule> rules = {
        ruleOf(BUSY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, other),
        ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN, stay),
    };
    const KiottyNetStateMachine machine = machineOf(rules);
    const net::RuleResult found = net::findRule(machine, IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ);
    EXPECT_EQ(KIOTTY_NET_CODE_UNHANDLED_EVENT, found.code);
}

TEST(RuleTable, IsAllowedWithoutNextListIsTrue)
{
    const KiottyNetRule open = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay);
    EXPECT_TRUE(net::isAllowed(open, DONE));
    const KiottyNetRule listed = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay, { BUSY, DONE });
    EXPECT_TRUE(net::isAllowed(listed, DONE));
    EXPECT_FALSE(net::isAllowed(listed, IDLE));
}

TEST(RuleTable, CheckRulesRejectsNullMachineRulesAndZeroCount)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::checkRules(nullptr));
    const KiottyNetStateMachine no_rules{ nullptr, 1, IDLE };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::checkRules(&no_rules));
    const KiottyNetRule rule = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay);
    const KiottyNetStateMachine empty{ &rule, 0, IDLE };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::checkRules(&empty));
}

TEST(RuleTable, CheckRulesRejectsDuplicateStateEvent)
{
    const std::vector<KiottyNetRule> rules = {
        ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay),
        ruleOf(BUSY, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, other),
        ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, other),
    };
    const KiottyNetStateMachine machine = machineOf(rules);
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, net::checkRules(&machine));
}

TEST(RuleTable, CheckRulesRejectsNullActionUnknownEventAndBadStates)
{
    const KiottyNetRule null_action = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, nullptr);
    const KiottyNetRule unknown_event = ruleOf(IDLE, static_cast<KiottyNetConnectionEventType>(6), stay);
    const KiottyNetRule low_state = ruleOf(-2, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay);
    const KiottyNetRule low_next = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay, { -2 });
    KiottyNetRule many_next = ruleOf(IDLE, KIOTTY_NET_CONNECTION_EVENT_TYPE_READ, stay);
    many_next.next.count = KIOTTY_NET_MAX_NEXT + 1;
    for (const KiottyNetRule& rule : { null_action, unknown_event, low_state, low_next, many_next })
    {
        const KiottyNetStateMachine machine{ &rule, 1, IDLE };
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::checkRules(&machine));
    }
}

TEST(RuleTable, CheckRulesAcceptsTheSortedCppTable)
{
    static constexpr auto RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(stay).to(IDLE),
        KiottyNet_from(IDLE).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(other).to(BUSY),
        KiottyNet_from(BUSY).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN).then(stay).to(IDLE),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(stay));
    static constexpr KiottyNetStateMachine MACHINE = KiottyNet_makeStateMachine(RULES, IDLE);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::checkRules(&MACHINE));
}
