
#include "playerbot/playerbot.h"
#include "NaxxramasDungeonTriggers.h"

using namespace ai;
#include "playerbot/strategy/actions/NaxxramasDungeonActions.h"
bool MaexxnaRescueTrigger::IsActive()
{
    Unit* wrap = GetMaexxnaRescueTarget(ai);
    return wrap && wrap != AI_VALUE(Unit*, "current target");
}

#include "playerbot/LocustSwarmPolicy.h"
bool LocustSwarmTrigger::IsActive()
{
    Unit* boss = GetActiveLocustSwarm(ai);
    return boss && LocustSwarmNeedsRetreat(bot->GetCombatDistance(boss));
}

#include "playerbot/strategy/values/GrobbulusCloudHazards.h"
bool GrobbulusCloudTrigger::IsActive() { return InsideGrobbulusCloud(ai); }
