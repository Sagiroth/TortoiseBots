
#include "playerbot/playerbot.h"
#include "LowerKarazhanDungeonTriggers.h"

using namespace ai;

namespace
{
    // Longest an egg wave can go unseen by a bot that copies its attackers.
    constexpr uint32 kAraxxnaEggRefreshSecs = 5;

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
    // list "possible targets" (same qualifier as AttackersValue::AddTargetsOf:
    // range, no validate). A bot that scans its own attackers refreshes that
    // list anyway; one that copies a neighbour's attackers never does, so
    // rescan it here at most once per kAraxxnaEggRefreshSecs (eggs take 20s
    // to hatch) and read the cached list in between.
    const std::string qualifier = Qualified::MultiQualify(
        { std::to_string((int32)sPlayerbotAIConfig.sightDistance), std::to_string(true) }, ":");
    Value<std::list<ObjectGuid>>* nearby = context->GetValue<std::list<ObjectGuid>>("possible targets", qualifier);
    if (!nearby)
        return false;
    UntypedValue* nearbyCache = dynamic_cast<UntypedValue*>(nearby);
    const bool stale = !nearbyCache || nearbyCache->Expired(kAraxxnaEggRefreshSecs);
    for (const ObjectGuid& guid : stale ? nearby->Get() : nearby->LazyGet())
    {
        if (IsAraxxnaBrood(ai->GetUnit(guid), bot))
            return true;
    }
    return false;
}
