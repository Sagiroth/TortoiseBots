#include "playerbot/playerbot.h"
#include "AyamissLarvaTarget.h"
#include "Group/Group.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "RaidDpsTargetPolicy.h"

using namespace ai;

namespace
{
    class AyamissTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit AyamissTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
        {
            if (!IsRaidDpsTargetExcluded(ai, attacker))
                FindLeastHpTargetStrategy::CheckAttacker(attacker, threatManager);
        }
    };

}

namespace ai
{
    Unit* GetAyamissLarvaTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 509 || !bot->GetGroup() || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("ayamiss larva focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        // Only a larva already threatening a paralyzed group member is a rescue target.
        // Do not depend on the flying boss being present in the damage-target cache.
        AyamissTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15555 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target"))
                continue;
            Unit* victim = candidate->GetVictim();
            if (!victim || !victim->IsPlayer() || !victim->IsAlive() || !victim->IsInMap(bot) ||
                !victim->HasAura(25725) || !bot->GetGroup()->IsMember(victim->GetObjectGuid()))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

}
