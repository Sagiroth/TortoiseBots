#include "playerbot/playerbot.h"
#include "SapphironDungeonTriggers.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindSapphiron(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15989)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15989, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }
}

bool SapphironAirTrigger::IsActive()
{
    Unit* boss = FindSapphiron(ai, bot);
    if (!boss)
        return false;
    return boss->IsHovering();
}

bool SapphironBlizzardTrigger::IsActive()
{
    return ai->HasAura(28534, bot) || ai->HasAura(28547, bot);
}

bool SapphironFlankTrigger::IsActive()
{
    if (ai->IsTank(bot) || ai->IsRanged(bot))
        return false;
    Unit* boss = FindSapphiron(ai, bot);
    if (!boss || boss->IsHovering())
        return false;
    return bot->IsWithinDistInMap(boss, 12.0f);
}
