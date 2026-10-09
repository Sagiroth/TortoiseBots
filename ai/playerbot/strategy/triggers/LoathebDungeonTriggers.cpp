#include "playerbot/playerbot.h"
#include "LoathebDungeonTriggers.h"
#include "playerbot/LoathebSporesPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool LoathebSporeTrigger::IsActive()
{
    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 16286, 5.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 5.0f);
    for (Unit* unit : nearby)
    {
        if (!unit || !unit->IsAlive())
            continue;
        if (ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            return true;
    }
    return false;
}

bool LoathebPositionTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return bot->GetDistance2d(2877.57f, -3967.00f) > 3.0f;
    if (ai->IsRanged(bot))
        return bot->GetDistance2d(2896.96f, -3980.61f) > 5.0f;
    return false;
}
