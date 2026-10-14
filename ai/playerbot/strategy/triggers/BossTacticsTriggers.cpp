#include "playerbot/playerbot.h"
#include "BossTacticsTriggers.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "../../../../runtime/CommandedGroupQuery.h"

using namespace ai;

namespace
{
bool TacticsWanted(Player* bot)
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    // Map first: off every map without rows, and the commander lookup only runs
    // where it could matter.
    const bool mapHasRows = GetBossTactics().HasMap(bot->GetMapId());
    if (!mapHasRows)
        return false;
    const bool optIn = sPlayerbotAIConfig.dungeonTactics;
    return BossTacticsWanted(optIn, !optIn && TortoiseBots::BotCommands::IsInCommandedGroup(bot), mapHasRows);
}

bool Ready(PlayerbotAI* ai, Player* bot)
{
    return bot->IsInWorld() && !bot->IsBeingTeleported() && sServerFacade.IsAlive(bot) &&
           GetBossTactics().HasMap(bot->GetMapId()) && ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT);
}
}

bool BossTacticsEnterTrigger::IsActive()
{
    return !ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) && TacticsWanted(bot);
}

bool BossTacticsLeaveTrigger::IsActive()
{
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    // A bot in a teleport or loading screen keeps the strategy until it lands.
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    return !TacticsWanted(bot);
}

bool BossTacticsInterruptTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    return !fight.rows.empty() && FindBossInterrupt(ai, bot, fight).caster != nullptr;
}

bool BossTacticsKillFirstTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    Unit* pick = FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot));
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "current target") != pick;
}

bool BossTacticsFrontTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    return !fight.rows.empty() && FindBossFrontDanger(ai, bot, fight) != nullptr;
}
