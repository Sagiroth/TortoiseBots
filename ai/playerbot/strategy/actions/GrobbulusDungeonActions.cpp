#include "playerbot/playerbot.h"
#include "GrobbulusDungeonActions.h"
#include "playerbot/GrobbulusCloudPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool GrobbulusGoBehindAction::Execute(Event& event)
{
    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 15931, 100.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 100.0f);

    Unit* boss = nullptr;
    for (Unit* unit : nearby)
    {
        if (unit && unit->IsAlive())
        {
            boss = unit;
            break;
        }
    }
    if (!boss)
        return false;

    float x, y;
    GrobbulusBehindSpot(boss->GetPositionX(), boss->GetPositionY(),
        boss->GetOrientation(), x, y);
    if (bot->GetDistance2d(x, y) < 2.0f)
        return false;
    return MoveTo(bot->GetMapId(), x, y, bot->GetPositionZ());
}
