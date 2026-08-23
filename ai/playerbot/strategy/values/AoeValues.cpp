
#include "playerbot/playerbot.h"
#include "AoeValues.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
using namespace ai;

std::list<ObjectGuid> AoeCountValue::FindMaxDensity(Player* bot, float range)
{
    int maxCount = 0;
    ObjectGuid maxGroup;
    std::map<ObjectGuid, std::list<ObjectGuid> > groups;
    if (bot)
    {
        std::list<ObjectGuid> units = *PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers");
        
        for (std::list<ObjectGuid>::iterator i = units.begin(); i != units.end(); ++i)
        {
            Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
            if (unit)
            {
                float distanceToPlayer = sServerFacade.getDistance2d(unit, bot);
                if (sServerFacade.IsDistanceLessOrEqualThan(distanceToPlayer, range))
                {
                    for (std::list<ObjectGuid>::iterator j = units.begin(); j != units.end(); ++j)
                    {
                        Unit* other = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*j);
                        if (other)
                        {
                            float d = sServerFacade.getDistance2d(unit, other);
                            if (sServerFacade.IsDistanceLessOrEqualThan(d, sPlayerbotAIConfig.aoeRadius * 2.0f))
                            {
                                groups[*i].push_back(*j);
                            }
                        }
                    }

                    if (maxCount < groups[*i].size())
                    {
                        maxCount = groups[*i].size();
                        maxGroup = *i;
                    }
                }
            }
        }
    }

    if (!maxCount)
    {
        return std::list<ObjectGuid>();
    }

    return groups[maxGroup];
}

WorldLocation AoePositionValue::Calculate()
{
    std::list<ObjectGuid> group = AoeCountValue::FindMaxDensity(bot);
    if (group.empty())
        return WorldLocation();

    // Note: don't know where these values come from or even used.
    float x1, y1, x2, y2;
    for (std::list<ObjectGuid>::iterator i = group.begin(); i != group.end(); ++i)
    {
        Unit* unit = PlayerbotAIStorage::Instance().GetAI(bot)->GetUnit(*i);
        if (!unit)
            continue;

        if (i == group.begin() || x1 > unit->getPositionX())
            x1 = unit->getPositionX();
        if (i == group.begin() || x2 < unit->getPositionX())
            x2 = unit->getPositionX();
        if (i == group.begin() || y1 > unit->getPositionY())
            y1 = unit->getPositionY();
        if (i == group.begin() || y2 < unit->getPositionY())
            y2 = unit->getPositionY();
    }
    float x = (x1 + x2) / 2;
    float y = (y1 + y2) / 2;
    float z = bot->getPositionZ() + CONTACT_DISTANCE;;
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
