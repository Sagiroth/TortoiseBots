#include "playerbot/playerbot.h"
#include "LoathebDungeonActions.h"
#include "playerbot/LoathebSporesPolicy.h"
#include "AttackAction.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool LoathebChooseTargetAction::Execute(Event& event)
{
    Unit* boss = nullptr;
    Unit* spore = nullptr;

    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (unit->GetEntry() == 16011)
            boss = unit;
        else if (unit->GetEntry() == 16286 && ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            spore = unit;
    }
    // Spores may not be on anyone's target list yet: 5yd sweep by entry.
    if (!spore)
    {
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 16286, 5.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 5.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive() && ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            {
                spore = unit;
                break;
            }
        }
    }

    Unit* want = spore ? spore : boss;
    if (!want)
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}

bool LoathebPositionAction::Execute(Event& event)
{
    if (PlayerbotAI::IsTank(bot))
    {
        if (bot->GetDistance2d(2877.57f, -3967.00f) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), 2877.57f, -3967.00f, bot->GetPositionZ());
    }
    if (ai->IsRanged(bot))
    {
        if (bot->GetDistance2d(2896.96f, -3980.61f) < 5.0f)
            return false;
        return MoveTo(bot->GetMapId(), 2896.96f, -3980.61f, bot->GetPositionZ());
    }
    return false;
}
