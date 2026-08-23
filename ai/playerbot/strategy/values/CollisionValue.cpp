
#include "playerbot/playerbot.h"
#include "CollisionValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool CollisionValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target)
        return false;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.contactDistance;
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    for (std::list<Unit*>::iterator i = targets.begin(); i != targets.end(); ++i)
    {
        Unit* target = *i;
        if (bot == target) continue;

        if (!target->IsVisibleFor(bot, bot))
            continue;

        float dist = sServerFacade.getDistance2d(bot, target->getPositionX(), target->getPositionY());
        if (sServerFacade.IsDistanceLessThan(dist, target->getObjectBoundingRadius())) return true;
    }

    return false;
}
