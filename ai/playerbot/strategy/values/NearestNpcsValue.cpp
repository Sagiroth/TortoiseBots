
#include "playerbot/playerbot.h"
#include "NearestNpcsValue.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestNpcsValue::FindUnits(std::list<Unit*> &targets)
{
    AnyUnitInObjectRangeCheck u_check(bot, range);
    UnitListSearcher<AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestNpcsValue::AcceptUnit(Unit* unit)
{
    return !sServerFacade.IsHostileTo(unit, bot) && !dynamic_cast<Player*>(unit);
}
