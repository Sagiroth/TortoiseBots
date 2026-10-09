
#include "playerbot/playerbot.h"
#include "RaidTargetValues.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    std::string ToLowerName(const std::string& name)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return lower;
    }

    // Substring match so "anub'rekhan" finds "Anub'Rekhan" and short
    // qualifiers ("loatheb") still hit. Empty qualifier never matches.
    bool NameMatches(const std::string& unitName, const std::string& qualifier)
    {
        if (qualifier.empty() || unitName.empty())
            return false;
        return ToLowerName(unitName).find(ToLowerName(qualifier)) != std::string::npos;
    }
}

Unit* FindTargetByNameValue::Calculate()
{
    if (qualifier.empty())
        return nullptr;

    // Cheap path: the shared attackers list is already cached per tick.
    const std::list<ObjectGuid> attackers =
        AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (NameMatches(unit->GetName(), qualifier))
            return unit;
    }

    // Fallback: grid sweep for a boss the raid fights but that has not
    // hit this bot yet (phase helpers need it before first aggro).
    std::list<Unit*> nearby;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, 100.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 100.0f);
    for (Unit* unit : nearby)
    {
        if (!unit || !unit->IsAlive())
            continue;
        if (NameMatches(unit->GetName(), qualifier))
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
        if (!unit || !unit->IsAlive())
            continue;
        Creature* creature = dynamic_cast<Creature*>(unit);
        if (!creature)
            continue;
        const CreatureInfo* info = creature->GetCreatureInfo();
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
