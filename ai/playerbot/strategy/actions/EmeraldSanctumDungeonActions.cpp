#include "playerbot/playerbot.h"
#include "EmeraldSanctumDungeonActions.h"
#include "playerbot/strategy/triggers/EmeraldSanctumDungeonTriggers.h"

using namespace ai;

bool SolniusAttackAddAction::Execute(Event& event)
{
    if (!AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return false;

    Unit* current = AI_VALUE(Unit*, "current target");
    if (IsSolniusAdd(current) && sServerFacade.IsAlive(current))
        return false;

    Unit* add = FindSolniusAdd(ai);
    if (!add)
        return false;
    return Attack(bot, add);
}
