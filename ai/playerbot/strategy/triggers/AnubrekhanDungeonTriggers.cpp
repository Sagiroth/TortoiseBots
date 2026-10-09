#include "playerbot/playerbot.h"
#include "AnubrekhanDungeonTriggers.h"
#include "playerbot/AnubrekhanSwarmPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindAnub(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15956)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15956, 100.0f);
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

bool AnubrekhanAddsTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 16573)
            return true;
    }
    return false;
}

bool AnubrekhanSwarmTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    Unit* boss = FindAnub(ai, bot);
    if (!boss)
        return false;
    return IsAnubrekhanSwarmUp(ai->HasAura(28785, boss));
}
