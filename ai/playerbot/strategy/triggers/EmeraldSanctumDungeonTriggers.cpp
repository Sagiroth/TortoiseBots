
#include "playerbot/playerbot.h"
#include "EmeraldSanctumDungeonTriggers.h"

using namespace ai;

namespace ai
{
bool IsSolniusAdd(Unit* unit)
{
    if (!unit)
        return false;
    switch (unit->GetEntry())
    {
        case 60743: // Sanctum Dragonkin
        case 60745: // Sanctum Wyrmkin
        case 60746: // Sanctum Scalebane
        case 61212: // Sanctum Suppressor
            return true;
        default:
            return false;
    }
}

Unit* FindSolniusAdd(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Unit* nearest = nullptr;
    float nearestDist = 0.0f;
    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!IsSolniusAdd(unit) || !sServerFacade.IsAlive(unit) || unit->IsFriendlyTo(bot))
            continue;
        const float dist = bot->GetDistance(unit);
        if (!nearest || dist < nearestDist)
        {
            nearest = unit;
            nearestDist = dist;
        }
    }
    return nearest;
}
}

bool SolniusAddsTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMapId() != 807)
        return false;
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    if (IsSolniusAdd(current) && sServerFacade.IsAlive(current))
        return false;
    return FindSolniusAdd(ai) != nullptr;
}
