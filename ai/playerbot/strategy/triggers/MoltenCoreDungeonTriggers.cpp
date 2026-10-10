
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GeddonInfernoPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "MoltenCoreDungeonTriggers.h"

using namespace ai;

bool GeddonInfernoTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    // Only during a live Geddon fight: the multiplier keeps covering the
    // Living Bomb carrier after Geddon dies, the trigger does not.
    if (!ai->HasStrategy("geddon", BotState::BOT_STATE_COMBAT))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (!attacker || attacker->GetEntry() != kGeddonEntry)
            continue;
        // The move action no-ops past 20y, and an unconditional trigger
        // keeps every already-safe bot attempting a failing move each tick
        // (same shape as the donor's range-gated Shazzrah trigger).
        return ShouldRunFromGeddonInferno(true, ai->HasAura(kInfernoSpellId, attacker)) &&
            bot->IsWithinDist(attacker, kGeddonRunoutDistance);
    }
    return false;
}