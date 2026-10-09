#include "playerbot/playerbot.h"
#include "GluthDungeonTriggers.h"
#include "playerbot/GluthKitePolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindGluth(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15932)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15932, 100.0f);
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

bool GluthMortalWoundSwapTrigger::IsActive()
{
    if (!PlayerbotAI::IsTank(bot))
        return false;

    Unit* target = GetTarget();
    if (!target || target->GetEntry() != 15932)
        return false;

    Unit* victim = target->GetVictim();
    if (!victim || victim == bot)
        return false;
    Player* victimPlayer = dynamic_cast<Player*>(victim);
    if (!victimPlayer || !PlayerbotAI::IsTank(victimPlayer))
        return false;

    Aura* wound = ai->GetAura(25646, victim);
    const uint32 stacks = wound ? wound->GetStackAmount() : 0;
    return ShouldGluthTauntSwap(true, true, true, stacks);
}

bool GluthChowUpTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 16360, 30.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 30.0f);
    for (Unit* unit : nearby)
    {
        if (!unit || !unit->IsAlive())
            continue;
        const float pct = 100.0f * unit->GetHealth() / unit->GetMaxHealth();
        if (IsGluthChowExecute(pct))
            return true;
    }
    return false;
}
