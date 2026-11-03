#include "playerbot/playerbot.h"
#include "FankrissSpawnTarget.h"
#include "../actions/FankrissFocusActions.h"
#include "../actions/FankrissRaidActions.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"

using namespace ai;

namespace ai
{
    Unit* GetFankrissSpawnTarget(PlayerbotAI* ai)
    {
        if (Unit* spawn = GetFankrissFocusSpawn(ai)) return spawn;
        if (Unit* boss = GetFankrissStackBoss(ai)) return boss;
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 531 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) || ai->IsHeal(bot) ||
            !ai->HasStrategy("fankriss spawn focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        // Engaged spawns can remain eligible without the boss in the cached list.
        FindLeastHpTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15630 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target") || !FankrissSpawnAtStack(ai, candidate))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

}
