
#include "playerbot/playerbot.h"
#include "LowerKarazhanDungeonTriggers.h"

using namespace ai;

namespace
{
    bool IsAraxxnaBrood(Unit* unit, Player* bot)
    {
        if (!unit || !sServerFacade.IsAlive(unit) || unit->IsFriendlyTo(bot))
            return false;
        switch (unit->GetEntry())
        {
            case 30008: // Skitterweb Egg
            case 61206: // Skitterweb Crawler
            case 61207: // Skitterweb Darkfang
            case 61209: // Skitterweb Leaper
                return true;
            default:
                return false;
        }
    }
}

bool AraxxnaSwarmTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMapId() != 532)
        return false;
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return false;

    // Hatched spiders zone-aggro (SetInCombatWithZone), so they are in
    // the bot's cached attackers.
    for (const ObjectGuid& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        if (IsAraxxnaBrood(ai->GetUnit(guid), bot))
            return true;
    }

    // Eggs fight nobody until hit, so they only show in the proximity
    // list the attackers value already builds for this bot every refresh
    // (same qualifier as AttackersValue::AddTargetsOf: range, no validate).
    // LazyGet reads that cache and never starts a scan of its own.
    const std::string qualifier = Qualified::MultiQualify(
        { std::to_string((int32)sPlayerbotAIConfig.sightDistance), std::to_string(true) }, ":");
    for (const ObjectGuid& guid : AI_VALUE2_LAZY(std::list<ObjectGuid>, "possible targets", qualifier))
    {
        if (IsAraxxnaBrood(ai->GetUnit(guid), bot))
            return true;
    }
    return false;
}
