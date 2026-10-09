#include "playerbot/playerbot.h"
#include "KelthuzadDungeonTriggers.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindKt(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15990)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15990, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }

    // Any add engaged: scan the already-cached possible-targets list, no
    // extra grid sweeps (adds aggro the raid, so they show up group-wide).
    bool AnyAddUp(PlayerbotAI* ai)
    {
        const std::list<ObjectGuid> targets =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (const ObjectGuid& guid : targets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit)
                continue;
            const uint32 entry = unit->GetEntry();
            if (entry == 16427 || entry == 16428 || entry == 16429 || entry == 16441)
                return true;
        }
        return false;
    }
}

bool KelthuzadAddsTrigger::IsActive()
{
    return AnyAddUp(ai);
}

bool KelthuzadPhaseTwoTrigger::IsActive()
{
    Unit* kt = FindKt(ai, bot);
    if (!kt)
        return false;
    // Vanilla: KT sits out phase 1 with NOT_SELECTABLE (core sets
    // IMMUNE_TO_PLAYER | NOT_SELECTABLE until phase 2), not the donor's
    // NON_ATTACKABLE.
    return !kt->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
}
