#include "playerbot/playerbot.h"
#include "RaidTargetValues.h"
#include "playerbot/RaidFrameworkPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

Unit* FindTargetByNameValue::Calculate()
{
    if (qualifier.empty())
        return nullptr;

    // The shared attackers list is group-wide ("in combat with the bot
    // (or bot group)"), already cached per tick — and engagement is the
    // point: like the donor (threat-list only), non-null implies the raid
    // is fighting the unit. No grid fallback: it would return bosses the
    // raid is NOT fighting (e.g. Loatheb while clearing trash), arming
    // suppression for a future fight.
    const std::list<ObjectGuid> attackers =
        AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (RaidNameMatches(unit->GetName(), qualifier))
            return unit;
    }

    return nullptr;
}

Unit* BossTargetValue::Calculate()
{
    Unit* closest = nullptr;
    float closestDist = 100.0f;

    std::list<Unit*> nearby;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, 100.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 100.0f);
    for (Unit* unit : nearby)
    {
        // Engagement gate (donor searches attackers only): a dormant
        // worldboss within 100yd must not arm fight suppression.
        if (!unit || !unit->IsAlive() || !unit->IsInCombat())
            continue;
        Creature* creature = dynamic_cast<Creature*>(unit);
        if (!creature)
            continue;
        const CreatureInfo* info = creature->GetCreatureInfo();
        // Rank 3 = WORLDBOSS in Turtle's creature_template (verified for
        // every AQ20/Naxx boss; adds are rank 0/1).
        if (!info || info->rank != CREATURE_ELITE_WORLDBOSS)
            continue;
        float dist = bot->GetDistance(unit);
        if (!closest || dist < closestDist)
        {
            closest = unit;
            closestDist = dist;
        }
    }

    return closest;
}
