#include "playerbot/playerbot.h"
#include "HuhuranActions.h"
#include "playerbot/HuhuranPolicy.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"

using namespace ai;
namespace
{
bool Has(PlayerbotAI* ai, char const* role)
{
    return ai->HasStrategy(role, BotState::BOT_STATE_COMBAT);
}
Unit* HuhuranBoss(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        bot->GetMapId() != 531 || !bot->GetGroup() ||
        (!Has(ai, "huhuran tank") && !Has(ai, "huhuran ranged"))) return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    // Existing group-attacker cache only: never start a fresh pull.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (!boss || boss->GetEntry() != 15509 || !boss->IsCreature() || !boss->IsAlive() ||
            !boss->IsInWorld() || !boss->IsInMap(bot) || !boss->IsInCombat() ||
            static_cast<Creature*>(boss)->IsInEvadeMode() || !bot->IsValidAttackTarget(boss)) continue;
        Unit* victim = boss->GetVictim();
        if (victim && victim->IsPlayer() && static_cast<Player*>(victim)->GetGroup() == bot->GetGroup())
            return boss;
    }
    return nullptr;
}
bool OrderedElsewhere(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    for (Unit* target : {ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target")), AI_VALUE(Unit*, "rti target")})
        if (target && target != boss && PossibleAttackTargetsValue::IsValid(target,
            ai->GetBot(), sPlayerbotAIConfig.sightDistance, false, false)) return true;
    return false;
}
bool Parked(PlayerbotAI* ai)
{
    return Has(ai, "stay") || Has(ai, "guard") || Has(ai, "follow") ||
        ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
}
char const* MissingStance(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetClass() == CLASS_WARRIOR && bot->HasSpell(71) && !ai->HasAura("defensive stance", bot))
        return "defensive stance";
    if (bot->GetClass() == CLASS_DRUID && !ai->HasAura("dire bear form", bot) && !ai->HasAura("bear form", bot))
    {
        if (bot->HasSpell(9634)) return "dire bear form";
        if (bot->HasSpell(5487)) return "bear form";
    }
    return nullptr;
}
Unit* RangedBoss(PlayerbotAI* ai)
{
    if (!Has(ai, "huhuran ranged") || ai->IsTank(ai->GetBot()) ||
        (!ai->IsRanged(ai->GetBot()) && !ai->IsHeal(ai->GetBot())) || Parked(ai) ||
        Has(ai, "wait for attack")) return nullptr;
    Unit* boss = HuhuranBoss(ai);
    // Never kite the boss through the soakers after taking aggro.
    return boss && boss->GetVictim() != ai->GetBot() && !OrderedElsewhere(ai, boss) ? boss : nullptr;
}
float RangedRadius(PlayerbotAI* ai, Unit* boss)
{
    std::vector<float> distances;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if (!member || member == ai->GetBot() || !member->IsInWorld() || !member->IsAlive() ||
            member->IsBeingTeleported() || !member->IsInMap(boss)) continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        // Exclude fellow protected ranged so they cannot push each other
        // ever farther away. The remaining roster is the operator's soakers.
        if (peer && Has(peer, "huhuran ranged") && !peer->IsTank(member) &&
            (peer->IsRanged(member) || peer->IsHeal(member))) continue;
        distances.push_back(boss->GetDistance2dToCenter(member));
    }
    return HuhuranRangedRadius(std::move(distances));
}
class HuhuranTauntMultiplier : public Multiplier
{
public:
    HuhuranTauntMultiplier(PlayerbotAI* ai) : Multiplier(ai, "huhuran no taunts") {}
    float GetValue(Action* action) override
    {
        return action && EbonrocTauntAction(action->getName()) && HuhuranBoss(ai) ? 0.0f : 1.0f;
    }
};
class HuhuranRangedMultiplier : public Multiplier
{
public:
    HuhuranRangedMultiplier(PlayerbotAI* ai) : Multiplier(ai, "huhuran ranged spacing") {}
    float GetValue(Action* action) override
    {
        if (!action) return 1.0f;
        Unit* boss = RangedBoss(ai);
        if (!boss) return 1.0f;
        std::string const name = action->getName();
        if (name == "follow" || name == "set behind" || name == "reach melee") return 0.0f;
        // Short offensive spells must not repeatedly walk back into the
        // soakers. Healing reach and genuine out-of-range recovery still run.
        if (name == "reach spell" && action->GetTarget() == boss &&
            bot->GetDistance2dToCenter(boss) <= RangedRadius(ai, boss) + 5.0f) return 0.0f;
        return 1.0f;
    }
};
}

Unit* ai::GetAssignedHuhuranTankTarget(PlayerbotAI* ai)
{
    if (!ai || !Has(ai, "huhuran tank") || !ai->IsTank(ai->GetBot()) || Parked(ai)) return nullptr;
    Unit* boss = HuhuranBoss(ai);
    return boss && !OrderedElsewhere(ai, boss) ? boss : nullptr;
}
bool HuhuranTankEngageAction::isUseful()
{
    Unit* boss = GetAssignedHuhuranTankTarget(ai);
    return boss && (MissingStance(ai) || AI_VALUE(Unit*, "current target") != boss ||
        bot->GetVictim() != boss || !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING));
}
bool HuhuranTankEngageAction::Execute(Event& event)
{
    Unit* boss = GetAssignedHuhuranTankTarget(ai);
    if (!boss || !isPossible()) return false;
    if (char const* stance = MissingStance(ai)) return ai->DoSpecificAction(stance, event, true);
    return Attack(GetMaster(), boss);
}
bool HuhuranRangedAction::isUseful()
{
    Unit* boss = RangedBoss(ai);
    return boss && MovementAction::isPossible() && !bot->IsNonMeleeSpellCasted(false, false, true) &&
        HuhuranNeedsSpace(bot->GetDistance2dToCenter(boss), RangedRadius(ai, boss));
}
bool HuhuranRangedAction::Execute(Event& /*event*/)
{
    if (!isUseful()) return false;
    Unit* boss = RangedBoss(ai);
    if (!boss) return false;
    float const radius = RangedRadius(ai, boss);
    WorldPosition const start(bot), center(boss);
    auto const hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    float const angle = center.GetAngleTo(start);
    for (float offset : {0.0f, 0.25f, -0.25f, 0.5f, -0.5f})
    {
        WorldPosition point = center + WorldPosition(0, radius * std::cos(angle + offset),
            radius * std::sin(angle + offset), 0.0f);
        point.setZ(point.GetHeight());
        if (!point.isValid() || std::fabs(point.getZ() - start.getZ()) > 5.0f ||
            !IsMovingAllowed(bot->GetMapId(), point.getX(), point.getY(), point.getZ()) ||
            std::any_of(hazards.begin(), hazards.end(), [&](HazardPosition const& h) {
                return point.distance(h.first) <= h.second;
            }) ||
            !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) ||
            !boss->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) ||
            !start.canPathTo(point, bot)) continue;
        // Keep ordinary path/hazard and unengaged-enemy checks enabled.
        if (MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ())) return true;
    }
    return false;
}
void HuhuranTankStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("huhuran tank engage needed",
        NextAction::array(0, new NextAction("huhuran tank engage", ACTION_HIGH + 3), NULL)));
}
void HuhuranTankStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void HuhuranTankStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new HuhuranTauntMultiplier(ai)); }
void HuhuranTankStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }
void HuhuranRangedStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("huhuran ranged too close",
        NextAction::array(0, new NextAction("huhuran ranged move", ACTION_HIGH + 4), NULL)));
}
void HuhuranRangedStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new HuhuranRangedMultiplier(ai)); }
