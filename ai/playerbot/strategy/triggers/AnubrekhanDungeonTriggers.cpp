#include "playerbot/playerbot.h"
#include "AnubrekhanDungeonTriggers.h"
#include "playerbot/AnubrekhanSwarmPolicy.h"

using namespace ai;

namespace
{
    // Cached lists only: during the encounter Anub is on threat
    // group-wide. No grid sweep per trigger tick.
    Unit* FindAnub(PlayerbotAI* ai, Player* bot)
    {
        (void)bot;
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15956)
                return unit;
        }
        const std::list<ObjectGuid> targets =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (const ObjectGuid& guid : targets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15956)
                return unit;
        }
        return nullptr;
    }
}

bool AnubrekhanAddsTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 16573)
            return true;
    }
    return false;
}

bool AnubrekhanSwarmTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    Unit* boss = FindAnub(ai, bot);
    if (!boss)
        return false;
    return IsAnubrekhanSwarmUp(ai->HasAura(28785, boss));
}
