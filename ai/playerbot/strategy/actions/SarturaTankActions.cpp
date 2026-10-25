#include "playerbot/playerbot.h"
#include "SarturaTankActions.h"
#include "SarturaWhirlwindActions.h"
#include "ReachTargetActions.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <algorithm>

using namespace ai;
namespace
{
bool SarturaTankEligible(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() &&
        bot->GetMapId() == 531 && bot->GetGroup() && ai->IsTank(bot, true);
}
bool SarturaOrdered(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (order && PossibleAttackTargetsValue::IsValid(order, ai->GetBot(),
        sPlayerbotAIConfig.reactDistance, false, false) &&
        !(order->GetEntry() == 15516 && ai->HasStrategy("sartura main tank", BotState::BOT_STATE_COMBAT))) return true;
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
}
bool SarturaEngaged(Unit* unit, Player* bot)
{
    if (!unit || !unit->IsCreature() || !unit->IsInWorld() || !unit->IsAlive() ||
        !unit->IsInCombat() || !unit->IsInMap(bot) ||
        static_cast<Creature*>(unit)->IsInEvadeMode() || !bot->IsValidAttackTarget(unit)) return false;
    Unit* victim = unit->GetVictim();
    return victim && victim->IsPlayer() && static_cast<Player*>(victim)->GetGroup() == bot->GetGroup();
}
bool SarturaSpinning(Unit* unit)
{
    // 26084 is the damage pulse, not the persistent boss aura.
    return unit && ((unit->GetEntry() == 15516 && unit->HasAura(26083)) ||
        (unit->GetEntry() == 15984 && unit->HasAura(26038)));
}
char const* SarturaMissingStance(PlayerbotAI* ai)
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
uint32 SarturaReadyTaunt(PlayerbotAI* ai, Unit* boss)
{
    if (!boss || boss->GetVictim() == ai->GetBot() || SarturaSpinning(boss) || SarturaMissingStance(ai)) return 0;
    char const* name = ai->GetBot()->GetClass() == CLASS_WARRIOR ? "taunt" :
        ai->GetBot()->GetClass() == CLASS_DRUID ? "growl" : nullptr;
    if (!name) return 0;
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const id = AI_VALUE2(uint32, "spell id", name);
    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
    if (!id || !spell || !ai->GetBot()->HasSpell(id)) return 0;
    bool taunt = false;
    for (int i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; ++i)
        if (spell->Effect[i] == SPELL_EFFECT_ATTACK_ME ||
            (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT))
        {
            taunt = true;
            if (boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(i), false)) return 0;
        }
    return taunt && ai->CanCastSpell(id, boss) ? id : 0;
}
}

Unit* ai::GetAssignedSarturaTankTarget(PlayerbotAI* ai)
{
    if (!SarturaTankEligible(ai) || SarturaOrdered(ai)) return nullptr;
    Player* bot = ai->GetBot();
    Player* main = nullptr;
    std::vector<Player*> tanks;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
        if (!member->IsInMap(bot) || !SarturaTankEligible(candidate) || SarturaOrdered(candidate)) continue;
        tanks.push_back(member);
        if (candidate->HasStrategy("sartura main tank", BotState::BOT_STATE_COMBAT))
        {
            if (main) return nullptr; // Ambiguous assignment: retain normal targeting.
            main = member;
        }
    }
    if (!main) return nullptr; // Entire feature is inert without an assigned living main tank.
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* boss = nullptr;
    std::vector<Unit*> guards;
    // Existing nearby-target cache only. Never pull an idle boss or guard.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (!SarturaEngaged(unit, bot)) continue;
        if (unit->GetEntry() == 15516) boss = unit;
        else if (unit->GetEntry() == 15984) guards.push_back(unit);
    }
    if (!boss) return nullptr;
    if (main == bot) return boss;
    if (guards.empty()) return nullptr; // Guards dead: ordinary tank targeting resumes.
    tanks.erase(std::remove(tanks.begin(), tanks.end(), main), tanks.end());
    auto byGuid = [](Unit* a, Unit* b) { return a->GetObjectGuid() < b->GetObjectGuid(); };
    std::sort(tanks.begin(), tanks.end(), byGuid);
    std::sort(guards.begin(), guards.end(), byGuid);
    auto self = std::find(tanks.begin(), tanks.end(), bot);
    return self == tanks.end() ? nullptr : guards[std::distance(tanks.begin(), self) % guards.size()];
}

bool SarturaTankRecoverAction::isUseful()
{
    Unit* boss = GetAssignedSarturaTankTarget(ai);
    if (!boss || boss->GetEntry() != 15516 || SarturaSpinning(boss) || SarturaWhirlwindTooClose(ai)) return false;
    return SarturaMissingStance(ai) || AI_VALUE(Unit*, "current target") != boss ||
        bot->GetVictim() != boss || !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING) ||
        !bot->CanReachWithMeleeAutoAttack(boss) || SarturaReadyTaunt(ai, boss);
}
bool SarturaTankRecoverAction::Execute(Event& event)
{
    Unit* boss = GetAssignedSarturaTankTarget(ai);
    if (!boss || !isPossible() || !isUseful()) return false;
    if (char const* stance = SarturaMissingStance(ai)) return ai->DoSpecificAction(stance, event, true);
    if (AI_VALUE(Unit*, "current target") != boss || bot->GetVictim() != boss ||
        !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING)) return Attack(GetMaster(), boss);
    if (uint32 spell = SarturaReadyTaunt(ai, boss)) return ai->CastSpell(spell, boss);
    return !bot->CanReachWithMeleeAutoAttack(boss) && ai->DoSpecificAction("reach melee", event, true);
}
float SarturaTankMultiplier::GetValue(Action* action)
{
    if (!action) return 1.0f;
    Unit* assigned = GetAssignedSarturaTankTarget(ai);
    if (!assigned) return 1.0f;
    std::string const name = action->getName();
    // Both escape implementations and emergency player movement remain usable.
    if (name == "avoid aoe" || name == "sartura whirlwind escape" || name == "move away from hazard" ||
        name == "raid bomb runout" || name == "flee" || name == "runaway") return 1.0f;
    bool const spinning = SarturaSpinning(assigned) || SarturaWhirlwindTooClose(ai);
    bool const movement = dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action);
    if (spinning && movement) return 0.0f;
    Unit* target = action->GetTarget();
    bool const encounterTarget = target && (target->GetEntry() == 15516 || target->GetEntry() == 15984);
    if (movement && encounterTarget && target != assigned) return 0.0f;
    // AoE taunts would collapse the boss/guard assignment even when aimed at self.
    if (name == "challenging shout" || name == "challenging roar" || name == "righteous defense") return 0.0f;
    if (encounterTarget && (dynamic_cast<AttackAction*>(action) || dynamic_cast<CastSpellAction*>(action) ||
        EbonrocTauntAction(name)))
        return target != assigned || spinning ? 0.0f : 1.0f;
    return 1.0f;
}
void SarturaMainTankStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Above ordinary tank assist (60), below both emergency step-outs (95/97).
    triggers.push_back(new TriggerNode("sartura tank recover needed",
        NextAction::array(0, new NextAction("sartura tank recover", 65.0f), nullptr)));
}
void SarturaMainTankStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void SarturaMainTankStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new SarturaTankMultiplier(ai)); }
void SarturaMainTankStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{ InitCombatMultipliers(multipliers); }
