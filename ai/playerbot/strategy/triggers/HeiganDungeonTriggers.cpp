#include "playerbot/playerbot.h"
#include "HeiganDungeonTriggers.h"
#include "playerbot/HeiganDancePolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindHeigan(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15936)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15936, 100.0f);
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

bool HeiganDanceTrigger::IsActive()
{
    Unit* heigan = FindHeigan(ai, bot);
    if (!heigan)
        return false;
    return IsHeiganDanceUp(ai->HasAura(29350, heigan));
}

bool HeiganPlatformHoldTrigger::IsActive()
{
    if (!ai->IsRanged(bot) && !ai->IsHeal(bot))
        return false;
    Unit* heigan = FindHeigan(ai, bot);
    if (!heigan)
        return false;
    return !IsHeiganDanceUp(ai->HasAura(29350, heigan));
}
