#include "playerbot/playerbot.h"
#include "RagnarosActions.h"
#include "Objects/TemporarySummon.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/PositionValue.h"

using namespace ai;

Unit* ai::GetRagnarosSonTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        bot->GetMapId() != 409 || !bot->IsInCombat() ||
        !ai->HasStrategy("ragnaros sons", BotState::BOT_STATE_COMBAT) ||
        (ai->IsHeal(bot) && !ai->IsTank(bot))) return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (order && PossibleAttackTargetsValue::IsValid(order, bot,
        sPlayerbotAIConfig.sightDistance, false, false)) return nullptr;
    if (AI_VALUE(Unit*, "rti target")) return nullptr;
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet())) return nullptr;

    Unit* current = AI_VALUE(Unit*, "current target");
    Unit* result = nullptr;
    float best = 0;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible attack targets"))
    {
        Unit* son = ai->GetUnit(guid);
        if (!son || son->GetEntry() != 12143 || !son->IsAlive() || !son->IsInCombat() ||
            !son->IsInMap(bot) || son->HasUnitState(UNIT_STAT_ISOLATED) ||
            PossibleAttackTargetsValue::HasBreakableCC(son, bot) ||
            PossibleAttackTargetsValue::HasUnBreakableCC(son, bot) ||
            !PossibleAttackTargetsValue::IsValid(son, bot, sPlayerbotAIConfig.sightDistance, false, false)) continue;
        auto* summon = dynamic_cast<TemporarySummon*>(son);
        if (!summon) continue;
        // The submerged boss disappears from ordinary hostile attacker lists.
        // Resolve this Son's real summoner instead of requiring a visible boss.
        Unit* boss = ai->GetUnit(summon->GetSummonerGuid());
        if (!boss || boss->GetEntry() != 11502 || !boss->IsAlive() || !boss->IsInCombat() ||
            !boss->IsInMap(bot) || !boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING) ||
            !boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)) continue;
        Player* victim = dynamic_cast<Player*>(son->GetVictim());
        bool const loose = !victim || !ai->IsTank(victim);
        float const score = (ai->IsTank(bot) && loose ? 1000.0f : 0.0f) +
            (son == current ? 10.0f : 0.0f) - sServerFacade.getDistance2d(bot, son);
        if (!result || score > best || (score == best && son->GetObjectGuid() < result->GetObjectGuid()))
        {
            result = son;
            best = score;
        }
    }
    return result;
}

bool RagnarosSonAttackAction::isUseful()
{
    Unit* target = GetTarget();
    return target && target != AI_VALUE(Unit*, "current target");
}
