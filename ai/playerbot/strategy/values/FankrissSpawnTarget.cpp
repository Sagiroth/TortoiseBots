#include "playerbot/playerbot.h"
#include "FankrissSpawnTarget.h"
#include "../actions/FankrissRaidActions.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "RaidDpsTargetPolicy.h"

using namespace ai;

namespace
{
    class FankrissTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit FankrissTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
        {
            if (!IsRaidDpsTargetExcluded(ai, attacker))
                FindLeastHpTargetStrategy::CheckAttacker(attacker, threatManager);
        }
    };

}

namespace ai
{
    Unit* GetFankrissSpawnTarget(PlayerbotAI* ai)
    {
        if (Unit* boss = GetFankrissStackBoss(ai)) return boss;
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 531 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("fankriss spawn focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        // Engaged spawns can remain eligible without the boss in the cached list.
        FankrissTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15630 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target"))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

}
