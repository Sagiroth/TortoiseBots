#include "playerbot/playerbot.h"
#include "RoleConsumableStrategy.h"
namespace ai
{
void RoleConsumableStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("often", NextAction::array(0, new NextAction("role consumable", 2.0f), nullptr)));
}
void RoleConsumableStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("critical health", NextAction::array(0, new NextAction("role consumable", ACTION_CRITICAL_HEAL), nullptr)));
    triggers.push_back(new TriggerNode("low health", NextAction::array(0, new NextAction("role consumable", ACTION_CRITICAL_HEAL), nullptr)));
    triggers.push_back(new TriggerNode("medium mana", NextAction::array(0, new NextAction("role consumable", ACTION_EMERGENCY), nullptr)));
    triggers.push_back(new TriggerNode("low mana", NextAction::array(0, new NextAction("role consumable", ACTION_EMERGENCY), nullptr)));
}
}
