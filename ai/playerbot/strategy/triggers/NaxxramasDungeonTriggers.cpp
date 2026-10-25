
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/KelThuzadFissureActions.h"
#include "NaxxramasDungeonTriggers.h"

using namespace ai;
#include "playerbot/strategy/actions/ThaddiusPolarityActions.h"
bool ThaddiusPolarityTrigger::IsActive()
{
    ThaddiusPolarityStackAction action(ai);
    return action.isUseful();
}

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
#include "playerbot/strategy/actions/SapphironBlizzardHazards.h"
bool LocalGrobbulusCloudTrigger::IsActive() { return InsideGrobbulusCloud(ai); }

#include "playerbot/strategy/actions/KelThuzadInterruptActions.h"
bool KelThuzadInterruptTrigger::IsActive()
{
    KelThuzadInterruptAction action(ai);
    return action.isUseful();
}

#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
bool PatchwerkMeleeTrigger::IsActive()
{
    PatchwerkMeleeRetreatAction action(ai);
    return action.isUseful();
}

bool ai::LocalSapphironBlizzardTrigger::IsActive() { return SapphironBlizzardTooClose(ai); }

bool KelThuzadFissureTrigger::IsActive()
{
    return InsideKelThuzadFissure(ai);
}
