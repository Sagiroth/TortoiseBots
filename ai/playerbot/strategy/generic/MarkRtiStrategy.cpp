
#include "playerbot/playerbot.h"
#include "MarkRtiStrategy.h"

using namespace ai;

void MarkRtiStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // mod-playerbots parity (LD-6): marking rides at NORMAL like the donor —
    // an EMERGENCY mark used to outrank defensive cooldowns.
    triggers.push_back(new TriggerNode(
        "no rti target",
        NextAction::array(0, new NextAction("mark rti", ACTION_NORMAL), NULL)));
}