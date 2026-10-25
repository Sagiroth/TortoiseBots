#include "playerbot/playerbot.h"
#include "SkeramImageTarget.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "RaidDpsTargetPolicy.h"

using namespace ai;

namespace
{
    class SkeramTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit SkeramTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
        {
            if (!IsRaidDpsTargetExcluded(ai, attacker))
                FindLeastHpTargetStrategy::CheckAttacker(attacker, threatManager);
        }
    };

}

namespace ai
{
    Unit* GetSkeramImageTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 531 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("skeram image focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        // The spell-created images share the original boss entry. Identify them
        // by the summon spell recorded by EffectSummonWild, not a health guess.
        SkeramTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15263 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                candidate->GetUInt32Value(UNIT_CREATED_BY_SPELL) != 747 ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target"))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

}
