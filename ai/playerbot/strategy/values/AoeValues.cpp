
#include "playerbot/playerbot.h"
#include "AoeValues.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
using namespace ai;

#include <set>

std::list<ObjectGuid> AoeCountValue::FindMaxDensity(Player* bot, float range)
{
    size_t maxCount = 0;
    ObjectGuid maxGroup;
    std::map<ObjectGuid, std::set<ObjectGuid> > groups;
    std::vector<ObjectGuid> uniqueUnits;
    if (bot)
    {
        std::list<ObjectGuid> units = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers");

        // Stable first-seen deduplication preserving attackers priority order
        std::set<ObjectGuid> seen;
        for (const ObjectGuid& guid : units)
        {
            if (seen.insert(guid).second)
                uniqueUnits.push_back(guid);
        }

        for (const ObjectGuid& guid : uniqueUnits)
        {
            Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(guid);
            if (unit)
            {
                float distanceToPlayer = sServerFacade.getDistance2d(unit, bot);
                if (sServerFacade.IsDistanceLessOrEqualThan(distanceToPlayer, range))
                {
                    for (const ObjectGuid& otherGuid : uniqueUnits)
                    {
                        Unit* other = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(otherGuid);
                        if (other)
                        {
                            float d = sServerFacade.getDistance2d(unit, other);
                            if (sServerFacade.IsDistanceLessOrEqualThan(d, sPlayerbotAIConfig.aoeRadius * 2.0f))
                            {
                                groups[guid].insert(otherGuid);
                            }
                        }
                    }

                    if (maxCount < groups[guid].size())
                    {
                        maxCount = groups[guid].size();
                        maxGroup = guid;
                    }
                }
            }
        }
    }

    if (!maxCount || maxGroup.IsEmpty())
    {
        return std::list<ObjectGuid>();
    }

    // Return the cluster members in original attackers priority order
    std::list<ObjectGuid> result;
    const std::set<ObjectGuid>& maxSet = groups[maxGroup];
    for (const ObjectGuid& guid : uniqueUnits)
    {
        if (maxSet.find(guid) != maxSet.end())
        {
            result.push_back(guid);
        }
    }

    return result;
}

WorldLocation AoePositionValue::Calculate()
{
    std::list<ObjectGuid> group = AoeCountValue::FindMaxDensity(bot);
    if (group.empty())
        return WorldLocation();

    // Note: don't know where these values come from or even used.
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    bool first = true;
    for (std::list<ObjectGuid>::iterator i = group.begin(); i != group.end(); ++i)
    {
        Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
        if (!unit)
            continue;

        if (first || x1 > unit->getPositionX())
            x1 = unit->getPositionX();
        if (first || x2 < unit->getPositionX())
            x2 = unit->getPositionX();
        if (first || y1 > unit->getPositionY())
            y1 = unit->getPositionY();
        if (first || y2 < unit->getPositionY())
            y2 = unit->getPositionY();
        first = false;
    }

    if (first)
        return WorldLocation();

    float x = (x1 + x2) / 2;
    float y = (y1 + y2) / 2;
    float z = bot->getPositionZ() + CONTACT_DISTANCE;
    bot->UpdateAllowedPositionZ(x, y, z);
    return WorldLocation(bot->GetMapId(), x, y, z, 0);
}

uint8 AoeCountValue::Calculate()
{
    return FindMaxDensity(bot).size();
}

bool HasAreaDebuffValue::Calculate()
{
    if (!GetTarget())
        return false;

    Unit* checkTarget = GetTarget();
    if (!checkTarget)
        return false;

    std::list<ObjectGuid> nearestDynObjects = *context->GetValue<std::list<ObjectGuid> >("nearest dynamic objects no los");
    if (nearestDynObjects.empty())
        return false;

    for (std::list<ObjectGuid>::iterator i = nearestDynObjects.begin(); i != nearestDynObjects.end(); ++i)
    {
        DynamicObject* go = checkTarget->GetMap()->GetDynamicObject(*i);
        if (!go)
            continue;

        SpellEntry const* spellProto = sSpellTemplate.LookupEntry<SpellEntry>(go->GetSpellId());
        if (!spellProto)
            continue;

        if (IsPositiveEffect(spellProto, go->GetEffIndex()))
            continue;

        if (go->IsAffecting(checkTarget))
            return true;
    }

    return false;
}
