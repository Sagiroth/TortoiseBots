#include "playerbot/playerbot.h"
#include "LoathebDungeonStrategies.h"
#include "playerbot/strategy/actions/LoathebDungeonActions.h"

using namespace ai;

void LoathebFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Spore under the bot: kill it for Fungal Bloom, else boss.
    triggers.push_back(new TriggerNode(
        "loatheb spore",
        NextAction::array(0, new NextAction("loatheb choose target", ACTION_HIGH + 1), NULL)));

    // Tank / ranged anchors.
    triggers.push_back(new TriggerNode(
        "loatheb position",
        NextAction::array(0, new NextAction("loatheb position", ACTION_HIGH), NULL)));
}

void LoathebFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end loatheb fight",
        NextAction::array(0, new NextAction("disable loatheb fight strategy", 100.0f), NULL)));
}

void LoathebFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end loatheb fight",
        NextAction::array(0, new NextAction("disable loatheb fight strategy", 100.0f), NULL)));
}

void LoathebFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new LoathebSporeHoldMultiplier(ai));
}
