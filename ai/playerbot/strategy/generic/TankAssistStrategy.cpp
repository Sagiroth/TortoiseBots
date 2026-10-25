
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/SarturaTankActions.h"
#include "TankAssistStrategy.h"

using namespace ai;

void TankAssistStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "tank assist",
        NextAction::array(0, new NextAction("tank assist", 60.0f), NULL)));
}

void TankAssistStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    // Inert unless this group has an active Sartura main-tank assignment.
    multipliers.push_back(new SarturaTankMultiplier(ai));
}
void TankAssistStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}
