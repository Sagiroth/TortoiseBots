#include "playerbot/playerbot.h"
#include "HeiganDungeonTriggers.h"
#include "playerbot/strategy/HeiganDungeonHelper.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/CombatSpreadPolicy.h"

using namespace ai;

bool HeiganDanceTrigger::IsActive()
{
    Unit* heigan = FindHeiganBoss(ai, bot);
    return IsHeiganDancing(ai, heigan);
}

bool HeiganPlatformHoldTrigger::IsActive()
{
    if (!ai->IsRanged(bot) && !ai->IsHeal(bot))
        return false;
    // Explicit orders win: a bot parked by its player (stay/follow/
    // wait/grind) or answering to a live master holds position — same
    // exemptions as the ranged-spread rule. Only the lethal dance is
    // forced.
    if (!ShouldCombatSpread(sServerFacade.IsInCombat(bot), ai->HasRealPlayerMaster(),
        ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT)) ||
        IsSpreadExemptOwned(ai->HasRealPlayerMaster(), ai->IsOwnedBot()))
        return false;
    Unit* heigan = FindHeiganBoss(ai, bot);
    if (!heigan)
        return false;
    return !IsHeiganDancing(ai, heigan);
}
