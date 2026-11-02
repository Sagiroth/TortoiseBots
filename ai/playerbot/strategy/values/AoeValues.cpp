
#include "playerbot/playerbot.h"
#include "AoeValues.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/combat/AttackersAoeDensityPolicy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
using namespace ai;

#include <set>

std::list<ObjectGuid> AoeCountValue::FindMaxDensity(Player* bot, float range)
{
    if (!bot)
        return std::list<ObjectGuid>();

    PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
    std::list<ObjectGuid> units = *botAi->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers");

    // A linked_death member the tactic holds (at or below the row's param % while another living member stands above
    // it) is left out of the density: an AoE is not aimed at a member that still has to live to the burn.
    if (sPlayerbotAIConfig.dungeonRunLinkedDeath && botAi->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
    {
        const BossLinkedDeathScan scan = ScanBossLinkedDeath(botAi, bot);
        if (scan.active)
        {
            std::list<ObjectGuid> kept;
            for (const ObjectGuid& guid : units)
            {
                // The group's own rule, by guid: an unrelated low-health attacker beside a healthy member is no
                // member and stays in the density.
                if (LinkedDeathHoldsMember(scan.param, scan.members, guid.GetRawValue()))
                    continue;
                kept.push_back(guid);
            }
            units = kept;
        }
    }

    // Dedup, cluster scoring, tie-breaking and result ordering are the pure
    // rules in AttackersAoeDensityPolicy; lookup and distance stay here.
    return FindMaxDensityCluster<ObjectGuid, Unit>(units,
        [botAi](const ObjectGuid& guid) -> Unit*
        {
            return botAi->GetUnit(guid);
        },
        [](Unit* unit, Unit* other)
        {
            return sServerFacade.getDistance2d(unit, other);
        },
        bot, range, sPlayerbotAIConfig.aoeRadius, sPlayerbotAIConfig.targetPosRecalcDistance);
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
