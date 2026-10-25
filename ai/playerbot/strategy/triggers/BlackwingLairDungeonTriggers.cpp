#include "playerbot/playerbot.h"
#include "BlackwingLairDungeonTriggers.h"
#include "playerbot/strategy/actions/BlackwingLairDungeonActions.h"

using namespace ai;

bool EbonrocShadowSwapTrigger::IsActive()
{
    EbonrocShadowSwapAction action(ai);
    return action.isUseful();
}

bool VaelTankHandoverTrigger::IsActive()
{
    VaelastraszTankHandoverAction action(ai);
    return action.isUseful();
}
