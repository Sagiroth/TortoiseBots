#include "playerbot/playerbot.h"
#include "LoathebDungeonTriggers.h"
#include "playerbot/LoathebSporesPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    bool LoathebEngaged(PlayerbotAI* ai)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 16011)
                return true;
        }
        return false;
    }
}

bool LoathebSporeTrigger::IsActive()
{
    // No sweep unless Loatheb himself is engaged (cheap guid walk first,
    // so non-Loatheb combat never scans).
    if (!LoathebEngaged(ai))
        return false;
    // Cached lists first: an aggroed spore shows up without a sweep.
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 16286 &&
            ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            return true;
    }
    // Fallback 5yd sweep: fresh spores are neutral until attacked and
    // never land on cached lists. Tiny radius by design.
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
    // Explicit orders win: parked bots hold their spot.
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT))
        return false;
    if (PlayerbotAI::IsTank(bot))
    {
        // Donor rule: only the aggro holder takes the anchor; an
        // off-tank building threat must not be dragged off the boss.
        if (!AI_VALUE2(bool, "has aggro", "current target"))
            return false;
        return bot->GetDistance2d(2877.57f, -3967.00f) > 3.0f;
    }
    if (ai->IsRanged(bot))
        return bot->GetDistance2d(2896.96f, -3980.61f) > 5.0f;
    return false;
}
