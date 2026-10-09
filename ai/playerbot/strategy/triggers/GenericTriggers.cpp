#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/GroupBuffPolicy.h"
#include "playerbot/ReadyRebuffPolicy.h"
#include "playerbot/SurvivePolicy.h"
#include "GenericTriggers.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/AoeValues.h"
#include "playerbot/strategy/actions/AttackAction.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

#include <regex>

using namespace ai;

bool NoManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < 5;
}

bool LowManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana;
}

bool MediumManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana;
}

bool HighManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < 65;
}

bool HealerShouldAttackTrigger::IsActive()
{
    if (!bot->GetGroup())
        return true;

    if (AI_VALUE2(uint8, "health", "party member to heal") < sPlayerbotAIConfig.almostFullHealth)
        return false;

    if (!checkMana)
        return true;

    // Easy fights (low balance) keep a large reserve; hard ones allow more.
    // 65 is the "high mana" line used by HighManaTrigger.
    uint8 balance = AI_VALUE(uint8, "balance");
    uint32 manaThreshold = balance <= 50 ? 85 : (balance <= 100 ? 65 : sPlayerbotAIConfig.mediumMana);
    return !AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") >= manaThreshold;
}

bool AlmostFullManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") > 85;
}

bool RageAvailable::IsActive()
{
    return AI_VALUE2(uint8, "rage", "self target") >= amount;
}

bool EnergyAvailable::IsActive()
{
	return AI_VALUE2(uint8, "energy", "self target") >= amount;
}

bool ComboPointsAvailableTrigger::IsActive()
{
    return AI_VALUE2(uint8, "combo", "current target") >= amount;
}

bool LoseAggroTrigger::IsActive()
{
    if(!AI_VALUE2(bool, "has aggro", "current target"))
    {
        // Check if the aggro has been taken by another tank
        if(ai->IsTank(bot))
        {
            Unit* target = AI_VALUE(Unit*, "current target");
            if(target && !target->IsPlayer())
            {
                Unit* targetsTarget = target->GetVictim();
                if(targetsTarget && targetsTarget->IsPlayer())
                {
                    Player* targetsPlayerTarget = (Player*)targetsTarget;
                    return !ai->IsTank(targetsPlayerTarget);
                }
            }
        }

        return true;
    }

    return false;
}

bool HasAggroTrigger::IsActive()
{
    return AI_VALUE2(bool, "has aggro", "current target");
}

bool PanicTrigger::IsActive()
{
    // Cheap gates first (perf): health/mana are scalar value reads, while
    // IsInPvp walks the enemy-player grid scan. Same && verdict, only
    // reordered: a bot above critical health, or with mana to fight on,
    // refuses whatever the PvP scan finds.
    if (AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.criticalHealth)
        return false;
    if (AI_VALUE2(bool, "has mana", "self target") &&
        AI_VALUE2(uint8, "mana", "self target") >= sPlayerbotAIConfig.lowMana)
        return false;
    return !ai->IsInPvp();
}

bool CriticalHealthNoMasterTrigger::IsActive()
{
    // Pool-only: a real player master (owned/hired bots, dungeon/raid
    // groups) decides movement itself. Never in PvP either, like Panic.
    if (!ShouldFleeAtCriticalHealth(ai->HasRealPlayerMaster()))
        return false;
    if (ai->IsInPvp())
        return false;
    return AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.criticalHealth;
}

bool OutNumberedTrigger::IsActive()
{
    // Below level 10, Hunter has no viable kiting toolkit (ranged abilities have an 8yd
    // minimum range, no traps yet, only Concussive Shot to slow) - fleeing mid-fight can't
    // actually create distance, it just eats time while still taking hits. The correct
    // pre-10 Hunter playbook is: burst at range, then fight it out in melee once the mob
    // closes. Don't fire this trigger for low-level Hunters at all; PanicTrigger (separate,
    // narrower - critical health AND low/no mana) is untouched, so a genuine near-death
    // escape attempt still fires.
    if (bot->GetClass() == CLASS_HUNTER && bot->GetLevel() < 10)
        return false;

    // Druid has Rejuvenation/Healing Touch as a genuine pre-10 self-sufficiency fallback -
    // no need to preserve a flee/retreat option here either. Same reasoning as Hunter above,
    // different tool (self-heal instead of "no viable kiting toolkit").
    if (bot->GetClass() == CLASS_DRUID && bot->GetLevel() < 10)
        return false;

    // Mage has no kiting toolkit pre-10 either (Frost Nova is level 10, Blink is level 22) -
    // same reasoning as Hunter: fleeing can't create real distance yet, so don't bother.
    if (bot->GetClass() == CLASS_MAGE && bot->GetLevel() < 10)
        return false;

    // Priest gets real self-healing from level 1 (Renew, Heal), so the pre-10 case is the
    // same "self-sufficiency beats fleeing" reasoning as Druid. Additionally, when playing
    // solo (no group to preserve healing capacity for), that reasoning holds at any level -
    // there's no one else depending on this bot surviving longer by running instead of
    // healing through it.
    if (bot->GetClass() == CLASS_PRIEST && (bot->GetLevel() < 10 || !bot->GetGroup()))
        return false;

    // Don't trigger if the bot is a dungeon or raid
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMap()->IsDungeon() || bot->GetMap()->IsRaid())
        return false;

    // Don't trigger if the bot is in a raid group
    if (bot->GetGroup() && bot->GetGroup()->IsRaidGroup())
        return false;

    // Don't trigger if the bot is in a group with a real player
    if (bot->GetGroup() && ai->HasRealPlayerMaster())
        return false;

    // Being outmatched on paper is not by itself an emergency. The comparison
    // below weighs levels and headcount, and `dLevel * 200` overtakes the
    // friendly side as soon as a single opponent is two levels up - so a solo
    // bot at full health broke off from one mob before a blow had landed. That
    // is also the least useful moment to run: nothing has gone wrong yet, and
    // the fight may well have been winnable. Engage first and let this decide
    // whether to disengage once the fight has actually turned; at mediumHealth
    // there is still enough left to get away. PanicTrigger covers the genuine
    // near-death case separately, and it is deliberately narrower (critical
    // health *and* no mana). Tunable via AiPlayerbot.MediumHealth.
    if (AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth)
        return false;

    int32 botLevel = bot->GetLevel();
    float healthMod = bot->GetHealthPercent() / 100.0f;
    uint32 friendPower = 100 + 100 * healthMod, foePower = 0;
    // Only mobs actually fighting this bot count, like the donor (mod-playerbots
    // weighs "attackers"). The old loop weighed every hostile in "possible attack
    // targets", so idle mobs standing near a fair one-on-one fight made a pool
    // bot under 70% health "outnumbered": it stopped swinging to flee, covered
    // no ground (median 0 yd) and died - 76% of deaths had a flee in their last
    // 30 s (Oct 2026 roster poll, 571 deaths). bot->GetAttackers() rather than
    // the "attackers" value, which also shares nearby players' targets.
    uint32 attackerCount = 0;
    for (Unit* attacker : bot->GetAttackers())
    {
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (!creature)
            continue;

        if (!creature->IsHostileTo(bot))
            continue;

        int32 dLevel = creature->GetLevel() - botLevel;

        healthMod = creature->GetHealthPercent() / 100.0f;

        if(dLevel > -10)
        {
            foePower += std::max(100 + 10 * dLevel, dLevel * 200) * healthMod;
            ++attackerCount;
        }
    }

    // Outnumbered means more than one: a pool bot does not run from a single
    // mob. Its flee cannot outpace the mob (median 0 yd covered, Oct 2026
    // poll), so breaking off a one-on-one only stops the swings - the weights
    // above made any mob two levels up "outnumber" a bot under 70% health.
    // The genuine near-death escape stays with the panic / critical health
    // triggers.
    if (!foePower || attackerCount < 2)
        return false;

    for (auto & helper : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get())
    {
        Unit* player = ai->GetUnit(helper);

        if (!player || player == bot)
            continue;

        int32 dLevel = player->GetLevel() - botLevel;

        healthMod = player->GetHealthPercent() / 100.0f;

        if (dLevel > -10 && sServerFacade.getDistance2d(bot, player) < 10.0f)
            friendPower += std::max(200 + 20 * dLevel, dLevel * 200)* healthMod;
    }

    return friendPower < foePower;
}

bool BuffTrigger::IsActive()
{
    // Cheap gates first (perf): the old order ran GetTarget (a "self target"
    // unit value read) before the two O(1) refuses. HasSpell is a spellbook
    // hit, the claim is two map lookups under one short lock; both are
    // cheaper than resolving and liveness-checking the target, and the aura
    // scan below is the most expensive step. Same verdict, only reordered:
    // a missing spell or a live claim refuses whatever the target is, and
    // IsTargetClaimedByOther already returns false for a null target.
    // A buff that was never trained can never appear as an aura, so without
    // this the trigger stays active forever and the cast fails every tick
    // (observed ACTION_LOOPs: inner fire / lightning shield / aspect of the
    // hawk on level 1-2 bots). No donor equivalent; native hardening for a
    // pool that starts at level 1 (donor bots are max-level, never affected).
    if (!ai->HasSpell(spell))
        return false;

    // Issue #T7: another bot is already casting this spell on the target (or, for
    // the area buffs, on the whole group). Stay inactive this tick rather than
    // pick the next member - that would re-create the same race for the others.
    if (BuffClaimRegistry::IsTargetClaimedByOther(bot, GetTarget(), spell))
        return false;

    Unit* target = GetTarget();
    if (!target || !target->IsAlive())
        return false;

    // Issue #468 (donor BuffBelowRefreshTarget): a LONG aura expiring inside
    // the refresh window counts as missing, so the buff is topped up on the
    // last out-of-combat tick instead of dropping mid-fight. Short combat
    // buffs (max < 5 min) only re-arm on fall-off, as before.
    Aura* aura = ai->GetAura(spell, target, checkIsOwner);
    return ai::BuffNeedsRefresh(aura != nullptr, aura ? aura->GetAuraDuration() : 0,
        aura ? aura->GetAuraMaxDuration() : 0);
}

bool MyBuffTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!ai->HasSpell(spell))
        return false;
    return target && !ai->HasMyAura(spell, target);
}

Value<Unit*>* BuffOnPartyTrigger::GetTargetValue()
{
    const std::string qualifier = spell + "-" + (ignoreTanks ? "1" : "0");
	return context->GetValue<Unit*>("party member without aura", qualifier);
}

Value<Unit*>* GreaterBuffOnPartyTrigger::GetTargetValue()
{
    // The greater buff only pays off for a member that still lacks the lower
    // single-target buff too: with the lower spell alone in the qualifier a
    // member that already has it (e.g. Power Word: Fortitude) would be picked,
    // the trigger would then fail its own lower-aura check, and the group
    // version would never be cast for the rest of the session (issue #378).
    // Spelled exactly like GreaterBuffOnPartyAction::GetTargetQualifier(), so the
    // trigger and the action always resolve the same target.
    const std::string qualifier = spell + (lowerSpell.empty() ? "" : "," + lowerSpell) + "-" + (ignoreTanks ? "1" : "0");
    return context->GetValue<Unit*>("party member without aura", qualifier);
}

Value<Unit*>* MyBuffOnPartyTrigger::GetTargetValue()
{
    const std::string qualifier = spell + "-" + (ignoreTanks ? "1" : "0");
    return context->GetValue<Unit*>("party member without my aura", qualifier);
}

ai::Value<Unit*>* BuffOnTankTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("party tank without aura", spell);
}

Value<Unit*>* DebuffOnAttackerTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("attacker without aura", spell);
}

bool NoAttackersTrigger::IsActive()
{
    return !AI_VALUE(Unit*, "current target") && AI_VALUE(uint8, "my attacker count") > 0;
}

bool InvalidTargetTrigger::IsActive()
{
    return AI_VALUE2(bool, "invalid target", "current target");
}

bool NoTargetTrigger::IsActive()
{
	return !AI_VALUE(Unit*, "current target") || AI_VALUE2(bool, "invalid target", "current target");
}

bool MasterTargetActiveTrigger::IsActive()
{
    // The follow strategy uses this to decide when to dps-assist the master.
    // "master target" is the master player (MasterTargetValue); active means
    // the master is alive and engaged with a live victim.
    Unit* master = AI_VALUE(Unit*, "master target");
    if (!master || !master->IsAlive())
        return false;

    Unit* victim = master->GetVictim();
    if (victim && victim->IsAlive())
        return true;

    // GetVictim is the melee swing target only: a master who opens with a
    // spell or a shot never has one, so the companions stood idle until the
    // mob reached them. Count the master's selected enemy too, once it is
    // fighting the master or someone in the master's group.
    if (master == bot || master->GetTypeId() != TYPEID_PLAYER || !master->IsInCombat())
        return false;
    Unit* selected = ai->GetUnit(static_cast<Player*>(master)->GetSelectionGuid());
    if (!selected || selected == bot || !selected->IsAlive() || !selected->IsInCombat() || !master->IsHostileTo(selected))
        return false;
    Unit* selectedVictim = selected->GetVictim();
    return selectedVictim && master->IsInRaidWith(selectedVictim);
}

bool MyAttackerCountTrigger::IsActive()
{
    return AI_VALUE2(bool, "combat", "self target") && AI_VALUE(uint8, "my attacker count") >= amount;
}

bool HighThreatTrigger::IsActive()
{
    uint8 relativeThreat = AI_VALUE2(uint8, "threat", "current target");

    if (relativeThreat >= 80)
    {
        //Watch delta.
        uint32 lastTime = MEM_AI_VALUE(float, "my threat::current target")->GetLastTime();

        if (lastTime < time(0))
        {
            float deltaThreat = LOG_AI_VALUE(float, "my threat::current target")->GetDelta(5.0f);
            float currentThreat = AI_VALUE2(float, "my threat", "current target");

            if (deltaThreat > 0)
            {
                float tankThreat = AI_VALUE2(float, "tank threat", "current target");

                float newThreat = currentThreat + deltaThreat * 5.0f; //No agro in 5 seconds.

                if (newThreat < tankThreat)
                    return false;
            }
        }

        return true;
    }

    return false;
}

bool MediumThreatTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "threat", "current target") >= 60)
        return true;

    return false;
}

bool SomeThreatTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "threat", "current target") >= 25)
        return true;

    return false;
}

bool NoThreatTrigger::IsActive()
{
    if (SomeThreatTrigger::IsActive())
        return false;

    return true;
}

// Deliberate damage-breakable CC that an AoE would waste. The frozen state is
// left out on purpose: Frost Nova sets it too, and nova + AoE is normal play.
static bool HoldsBreakableCc(PlayerbotAI* ai, Unit* unit, Player* bot)
{
    if (!unit || PossibleAttackTargetsValue::HasIgnoreCCRti(unit, bot))
        return false;

    static char const* const breakableCc[] = { "sap", "gouge", "shackle undead", "hibernate",
        "freezing trap effect", "seduction", "repentance", "wyvern sting" };
    if (unit->IsPolymorphed())
        return true;
    for (char const* spell : breakableCc)
        if (ai->HasAura(spell, unit))
            return true;
    return false;
}

bool AoeTrigger::IsActive()
{
    std::list<ObjectGuid> aoeEnemies = AoeCountValue::FindMaxDensity(bot, range);
    if (aoeEnemies.size() < (size_t)amount)
        return false;

    // CC interlock: never AoE a pack holding a breakable CC (sheep/sap/trap).
    // Unbreakable CC (stun/fear/roots) survives damage, so only breakable
    // blocks. Skull-marked mobs opted out of CC protection (HasIgnoreCCRti).
    // Splash counts too: a CCed mob beside the cluster still eats the blast.
    for (std::list<ObjectGuid>::iterator i = aoeEnemies.begin(); i != aoeEnemies.end(); ++i)
    {
        if (HoldsBreakableCc(ai, ai->GetUnit(*i), bot))
            return false;
    }
    std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!HoldsBreakableCc(ai, unit, bot))
            continue;
        for (std::list<ObjectGuid>::iterator j = aoeEnemies.begin(); j != aoeEnemies.end(); ++j)
        {
            Unit* member = ai->GetUnit(*j);
            if (member && sServerFacade.IsDistanceLessOrEqualThan(
                sServerFacade.getDistance2d(unit, member), sPlayerbotAIConfig.aoeRadius))
                return false;
        }
    }
    return true;
}

bool DebuffTrigger::IsActive()
{
    if (!ai->HasSpell(spell))
        return false;

    Unit* target = GetTarget();
    if(target && target->IsAlive())
    {
        if (!ai->HasAura(spell, target, false, checkIsOwner))
        {
            return !HasMaxDebuffs();
        }
    }

    return false;
}

bool DebuffTrigger::HasMaxDebuffs()
{
    Unit* target = GetTarget();
    if(target)
    {
        constexpr uint32 debuffLimit = 16;

        return ai->GetAuras(target, false, false).size() >= debuffLimit;
    }

    return false;
}

bool SpellTrigger::IsActive()
{
	return GetTarget();
}

bool SpellCanBeCastedTrigger::IsActive()
{
	Unit* target = GetTarget();
	// Ignore range/LOS here so this only gates on real cast-blockers (cooldown, mana,
	// proc/aura-state requirements). The action's own "reach spell" prerequisite is what
	// handles closing distance - if range were checked here too, the trigger would never
	// fire while out of range, so the reach step it depends on would never get a chance to run.
	return target && ai->CanCastSpell(spell, target, true, nullptr, true);
}

bool SpellNoCooldownTrigger::IsActive()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", name);
    if (!spellId)
        return false;

    return sServerFacade.IsSpellReady(bot, spellId);
}

bool RandomTrigger::IsActive()
{
    if (time(0) - lastCheck < sPlayerbotAIConfig.repeatDelay / 1000)
        return false;

    lastCheck = time(0);
    int k = (int)(probability / sPlayerbotAIConfig.randomChangeMultiplier);
    if (k < 1) k = 1;
    return (rand() % k) == 0;
}

bool AndTrigger::IsActive()
{
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(tname);
        if (!trigger1 || !trigger1->IsActive())
            return false;
    }

    return true;
}

std::string AndTrigger::getName()
{
    std::string name;
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        if (!name.empty())
            name += " and ";
        name += tname;
    }

    return name;
}

bool OrTrigger::IsActive()
{
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(tname);
        if (trigger1 && trigger1->IsActive())
            return true;
    }

    return false;
}

std::string OrTrigger::getName()
{
    std::string name;
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        if (!name.empty())
            name += " or ";
        name += tname;
    }

    return name;
}

bool TwoTriggers::IsActive()
{
    if (name1.empty() || name2.empty())
        return false;

    Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(name1);
    Trigger* trigger2 = ai->GetAiObjectContext()->GetTrigger(name2);

    if (!trigger1 || !trigger2)
        return false;

    return trigger1->IsActive() && trigger2->IsActive();
}

std::string TwoTriggers::getName()
{
    std::string name;
    name = name1 + " and " + name2;
    return name;
}

bool BoostTrigger::IsActive()
{
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT) && BuffTrigger::IsActive())
    {
        if (!ai->HasRealPlayerMaster())
        {
            return AI_VALUE(uint8, "balance") <= balance;
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool ItemCountTrigger::IsActive()
{
	return AI_VALUE2(uint32, "item count", item) < uint32(count);
}

bool InterruptSpellTrigger::IsActive()
{
	return SpellTrigger::IsActive() && ai->IsInterruptableSpellCasting(GetTarget(), getName(), true);
}

bool DeflectSpellTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (!target->IsNonMeleeSpellCasted(true))
        return false;

    if (target->GetTargetGuid() != bot->getObjectGuid())
        return false;

    uint32 spellid = context->GetValue<uint32>("spell id", spell)->Get();
    if (!spellid)
        return false;

    SpellEntry const *deflectSpell = sServerFacade.LookupSpellInfo(spellid);
    if (!deflectSpell)
        return false;

    // human priest feedback
    if (spell == "feedback")
        return true;

    SpellSchoolMask deflectSchool = SpellSchoolMask(deflectSpell->EffectMiscValue[0]);
    SpellSchoolMask attackSchool = SPELL_SCHOOL_MASK_NONE;

    Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (spell)
    {
        SpellEntry const* tarSpellInfo = spell->m_spellInfo;
        if (tarSpellInfo)
        {
            attackSchool = GetSpellSchoolMask(tarSpellInfo);
            if (deflectSchool == attackSchool)
                return true;
        }
    }
    return false;
}

bool HasAuraTrigger::IsActive()
{
   if (!name.empty())
	{
      return ai->HasAura(name, GetTarget(), false, false, -1, false, 0, auraTypeId);
   }

   std::string str = getQualifier();
   std::regex pattern(R"(spellid::(\d+)::([^:]*)::(\d+))");
   std::smatch match;

   if (std::regex_search(str, match, pattern) && match.size() == 4)
   {
      uint32 spell_id = atoi(match[1].str().c_str());

      if (Aura* aura = ai->GetAura(spell_id, GetTarget()))
      {
         uint32 count = atoi(match[3].str().c_str());
         uint32 stack_size = aura->GetStackAmount();
         std::string comp_symb = match[2].str();

         if (comp_symb == "equal")
         {
            return stack_size == count;
         }
         else if (comp_symb == "greater or equal")
         {
            return stack_size >= count;
         }
         else if (comp_symb == "lesser or equal")
         {
            return stack_size <= count;
         }
         else if (comp_symb == "greater")
         {
            return stack_size > count;
         }
         else if (comp_symb == "lesser")
         {
            return stack_size < count;
         }
         else
         {
            return false;
         }
      }
   };

   pattern = R"(spellid::(\d+))";

   if (std::regex_search(str, match, pattern) && match.size() == 2)
   {

      uint32 spell_id = atoi(match[1].str().c_str());
      return ai->HasAura(spell_id, GetTarget());
   }

   return false;
}

std::string HasAuraTrigger::getName()
{
   if (!name.empty())
   {
      return name;
   }

   std::ostringstream ss;
   ss << "has aura with " << getQualifier();
   return ss.str();
}


bool HasNoAuraTrigger::IsActive()
{
    return !ai->HasAura(getName(), GetTarget());
}

bool TankAssistTrigger::IsActive()
{
    if (!AI_VALUE(bool, "has attackers"))
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
        return true;

    // do not switch if enemy target
    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    if (enemy)
    {
        return currentTarget != enemy;
    }

    Unit* tankTarget = AI_VALUE(Unit*, "tank target");
    if (!tankTarget || currentTarget == tankTarget)
        return false;

    // mod-playerbots TankAssistTrigger semantics: switch only while the tank
    // still holds its current target. A loose add is picked up while the
    // current mob is held, and the tank can switch back to finish it later.
    // The old victim check forbade returning to a mob on the tank (one-way
    // door). "has aggro" is HasAggroValue (values/AttackerCountValues.cpp).
    bool holdsCurrent = AI_VALUE2(bool, "has aggro", "current target");
    // Finish a held mob that is already low before peeling a loose add; the
    // add waits a few seconds, a half-dead mob left behind waits forever.
    if (holdsCurrent && currentTarget->GetHealthPercent() <= sPlayerbotAIConfig.lowHealth)
        return false;
    return holdsCurrent;
}

bool IsBehindTargetTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && AI_VALUE2(bool, "behind", "current target");
}

bool IsNotBehindTargetTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && !AI_VALUE2(bool, "behind", "current target");
}

bool IsNotFacingTargetTrigger::IsActive()
{
    return !AI_VALUE2(bool, "facing", "current target");
}

bool TankFaceNeededTrigger::IsActive()
{
    // Scope: real-player-master parties only. Pool bots keep old behaviour.
    if (!ai->HasRealPlayerMaster())
        return false;
    if (!ai->IsTank(bot))
        return false;
    // Explicit holds win: a tank parked by stay/wait-for-attack does not
    // sidestep (mirrors the spread exemption in RaidSpreadNeededTrigger).
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
        return false;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsCreature() || !sServerFacade.IsAlive(target))
        return false;
    // Only while the tank holds the mob: it turns to face the tank, so
    // stepping to the far side of the mob points its front at the tank.
    if (!target->GetVictim() || target->GetVictim()->getObjectGuid() != bot->getObjectGuid())
        return false;
    if (!bot->CanReachWithMeleeAutoAttack(target) || target->IsMoving())
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    // Average angle from the mob to the live party (donor AverageGroupAngle).
    // Needs at least one other member: alone, there is nobody to protect.
    float sumX = 0.0f, sumY = 0.0f;
    int count = 0;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        sumX += member->GetPositionX() - target->GetPositionX();
        sumY += member->GetPositionY() - target->GetPositionY();
        ++count;
    }
    if (!count)
        return false;
    float averageAngle = atan2(sumY, sumX);
    // Hysteresis (donor TankFaceAction, tolerable = PI/2): fire only while
    // the mob's front points at the party side. After the sidestep the tank
    // sits ~108 degrees off, outside this window, so it does not jitter.
    float delta = averageAngle - target->GetAngle(bot);
    while (delta > M_PI)
        delta -= 2.0f * M_PI;
    while (delta < -M_PI)
        delta += 2.0f * M_PI;
    return fabs(delta) <= M_PI / 2.0f;
}

bool HasCcTargetTrigger::IsActive()
{
    uint32 spellid = AI_VALUE2(uint32, "spell id", getName());
    if (spellid && sServerFacade.IsSpellReady(bot, spellid))
    {
        // mod-playerbots d9ee5198/#2648: inside a non-raid dungeon the generic
        // CC never fires on a free pick — only on this bot's assigned raid
        // mark ("rti cc target"). Open world keeps today's free CC; raids
        // keep it too (marks are advisory there, packs are scripted).
        // Opt-in bypass: the "auto cc" strategy (OFF by default, toggled via
        // `.bot action auto cc [on|off]`) lets the bot CC its own smart pick
        // (loose add on a healer/caster, unattacked, undotted) in dungeons.
        // Explicit marks still win: CcTargetValue returns the assigned target
        // first, so an assigned bot never falls through to its auto pick.
        if (!ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) &&
            bot->IsInWorld() && bot->GetMap() && bot->GetMap()->IsDungeon() && !bot->GetMap()->IsRaid())
        {
            Unit* rtiCcTarget = AI_VALUE(Unit*, "rti cc target");
            if (!rtiCcTarget)
                return false;
            Unit* ccTarget = AI_VALUE2(Unit*, "cc target", getName());
            if (!ccTarget || ccTarget != rtiCcTarget)
                return false;
        }
        return AI_VALUE2(Unit*, "cc target", getName()) && !AI_VALUE2(Unit*, "current cc target", getName());
    }

    return false;
}

bool NoMovementTrigger::IsActive()
{
	return !AI_VALUE2(bool, "moving", "self target");
}

bool NoPossibleTargetsTrigger::IsActive()
{
    std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");
    return !targets.size();
}

bool PossibleAddsTrigger::IsActive()
{
    return AI_VALUE(bool, "possible adds") && !AI_VALUE(ObjectGuid, "attack target");
}

bool NotDpsTargetActiveTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target)
    {
        if (target->IsPlayer())
        {
            return false;
        }

        if(sServerFacade.IsAlive(target))
        {
            // do not switch if enemy target
            Unit* enemy = AI_VALUE(Unit*, "enemy player target");
            if (enemy)
            {
                return target != enemy;
            }

            Unit* dps = AI_VALUE(Unit*, "dps target");
            if (dps)
            {
                return target != dps;
            }
        }
    }

    return false;
}

bool NotDpsAoeTargetActiveTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target)
    {
        if (target->IsPlayer())
        {
            return false;
        }

        if (sServerFacade.IsAlive(target))
        {
            // do not switch if enemy target
            Unit* enemy = AI_VALUE(Unit*, "enemy player target");
            if (enemy)
            {
                return target != enemy;
            }

            Unit* dps = AI_VALUE(Unit*, "dps aoe target");
            if (dps)
            {
                return target != dps;
            }
        }
    }

    return false;
}

bool IsSwimmingTrigger::IsActive()
{
    return AI_VALUE2(bool, "swimming", "self target");
}

bool HasNearestAddsTrigger::IsActive()
{
    std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "nearest adds");
    return targets.size();
}

bool HasItemForSpellTrigger::IsActive()
{
    std::string spell = getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return spellId && AI_VALUE2(Item*, "item for spell", spellId);
}


bool TargetChangedTrigger::IsActive()
{
    Unit* oldTarget = context->GetValue<Unit*>("old target")->Get();
    Unit* target = context->GetValue<Unit*>("current target")->Get();
    return target && oldTarget != target;
}

Value<Unit*>* InterruptEnemyHealerTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("enemy healer target", spell);
}

Value<Unit*>* SnareTargetTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("snare target", spell);
}

bool StayTimeTrigger::IsActive()
{
    time_t stayTime = AI_VALUE(time_t, "stay time");
    time_t now = time(0);
    return delay && stayTime && now > stayTime + 2 * delay / 1000;
}

bool IsMountedTrigger::IsActive()
{
    return AI_VALUE2(bool, "mounted", "self target");
}

bool CorpseNearTrigger::IsActive()
{
    return bot->GetCorpse() && bot->GetCorpse()->IsWithinDistInMap(bot, CORPSE_RECLAIM_RADIUS, true);
}

bool IsFallingTrigger::IsActive()
{
    return bot->HasMovementFlag(MOVEFLAG_JUMPING);
}

bool IsFallingFarTrigger::IsActive()
{
    return bot->HasMovementFlag(MOVEFLAG_FALLINGFAR);
}

bool HasAreaDebuffTrigger::IsActive()
{
    return AI_VALUE2(bool, "has area debuff", "self target");
}

bool ReturnToStayPositionTrigger::IsActive()
{
    PositionEntry stayPosition = AI_VALUE(PositionMap&, "position")["stay"];
    if (stayPosition.isSet())
    {
        const float distance = bot->GetDistance(stayPosition.x, stayPosition.y, stayPosition.z);
        return distance > ai->GetRange("follow");
    }

    return false;
}

bool ReturnToPullPositionTrigger::IsActive()
{
    PullStrategy const* strategy = PullStrategy::Get(ai);
    if (!strategy || !strategy->HasPullActionCompleted())
        return false;
    // Only a pull whose recorded intent is a return (a pullback command, or
    // the tank's own "pull back" for an automatic pull) sends the tank back.
    if (!strategy->IsPullBackIntent())
        return false;

    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry pullPosition = posMap["pull"];
    return pullPosition.isSet() && pullPosition.mapId == bot->GetMapId() &&
           bot->GetDistance(pullPosition.x, pullPosition.y, pullPosition.z) > sPlayerbotAIConfig.followDistance;
}

bool NoBuffAndComboPointsAvailableTrigger::IsActive()
{
    if (BuffTrigger::IsActive())
    {
        return AI_VALUE2(uint8, "combo", GetComboPointsTargetName()) >= comboPoints;
    }

    return false;
}

bool InPvpTrigger::IsActive()
{
    return ai->IsInPvp();
}

bool InPveTrigger::IsActive()
{
    return ai->IsInPve();
}

bool InRaidFightTrigger::IsActive()
{
    return ai->IsInRaid();
}

bool GreaterBuffOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    Player* targetPlayer = dynamic_cast<Player*>(target);
    if (!targetPlayer || !IsInGroup_Helper(bot, targetPlayer))
        return false;
    // The group cast must actually land on this member: without the reagent
    // (or the training) the cast fails and the 60 s group retry starts, so a
    // member the area buff can never cover would block its single buff too.
    // Donor UpgradeToGroupIfAppropriate checks the same two facts per cast.
    if (!ai->HasSpell(spell) || AI_VALUE2(uint32, "has reagents for", AI_VALUE2(uint32, "spell id", spell)) == 0)
        return false;
    if (!BuffOnPartyTrigger::IsActive())
        return false;
    // Issue #468: the group buff only pays off while the member lacks the
    // lower single-target buff too - unless that LONG one is expiring inside
    // the refresh window, in which case the group cast tops up both at once.
    if (lowerSpell.empty())
        return true;
    Aura* lower = ai->GetAura(lowerSpell, target, checkIsOwner);
    return ai::BuffNeedsRefresh(lower != nullptr, lower ? lower->GetAuraDuration() : 0,
        lower ? lower->GetAuraMaxDuration() : 0);
}

bool TargetOfAttacker::IsActive()
{
    return !AI_VALUE(std::list<ObjectGuid>, "attackers targeting me").empty();
}

bool TargetOfAttackerInRange::IsActive()
{
    const Unit* closestAttacker = AI_VALUE(Unit*, "closest attacker targeting me");
    return closestAttacker && bot->GetCombatDistance(closestAttacker) <= (distance - sPlayerbotAIConfig.contactDistance);
}

bool TargetOfCastedAuraTypeTrigger::IsActive()
{
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        // Check against the given creature id
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker)
        {
            const Spell* auraTypeSpell = nullptr;
            const Spell* genericSpell = attacker->GetCurrentSpell(CurrentSpellTypes::CURRENT_GENERIC_SPELL);
            if (genericSpell)
            {
                const SpellEntry* spellInfo = genericSpell->m_spellInfo;
                if (spellInfo)
                {
                    for (int32 i = SpellEffectIndex::EFFECT_INDEX_0; i <= SpellEffectIndex::EFFECT_INDEX_2; i++)
                    {
                        if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA) && (spellInfo->EffectApplyAuraName[i] == auraType))
                        {
                            auraTypeSpell = genericSpell;
                            break;
                        }
                    }
                }
            }

            if (!auraTypeSpell)
            {
                const Spell* channeledSpell = attacker->GetCurrentSpell(CurrentSpellTypes::CURRENT_CHANNELED_SPELL);
                if (channeledSpell)
                {
                    const SpellEntry* spellInfo = channeledSpell->m_spellInfo;
                    if (spellInfo)
                    {
                        for (int32 i = SpellEffectIndex::EFFECT_INDEX_0; i <= SpellEffectIndex::EFFECT_INDEX_2; i++)
                        {
                            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA) && (spellInfo->EffectApplyAuraName[i] == auraType))
                            {
                                auraTypeSpell = channeledSpell;
                                break;
                            }
                        }
                    }
                }
            }

            if (auraTypeSpell)
            {
                Unit* spellTarget = auraTypeSpell->m_targets.getUnitTarget();
                if (spellTarget == bot)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool BuffOnTargetTrigger::IsActive()
{
    const Unit* target = GetTarget();
    return target && target->HasAura(buffID);
}

bool DispelOnTargetTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const uint32 dispelMask = GetDispellMask(dispelType);
        const std::vector<Aura*> auras = ai->GetAuras(target);
        for (const Aura* aura : auras)
        {
            const SpellEntry* spellInfo = aura->GetSpellProto();
            if (spellInfo && ((1 << spellInfo->Dispel) & dispelMask))
            {
                return true;
            }
        }
    }

    return false;
}

bool SpellTargetTrigger::IsActive()
{
    if (IsSpellReady())
    {
        // Check for assigned targets
        const std::list<ObjectGuid>& possibleTargets = AI_VALUE(std::list<ObjectGuid>, targetsValue);
        if (!possibleTargets.empty())
        {
            for (const ObjectGuid& possibleTargetGuid : possibleTargets)
            {
                if (IsTargetValid(ai->GetUnit(possibleTargetGuid)))
                {
                    return true;
                }
            }
        }
        else
        {
            // Check for the default target
            if (IsTargetValid(GetTarget()))
            {
                return true;
            }
        }
    }

    return false;
}

bool SpellTargetTrigger::IsTargetValid(Unit* target)
{
    Player* targetPlayer = dynamic_cast<Player*>(target);
    return target &&
           ai->IsSafe(target) &&
           (bot == target || sServerFacade.getDistance2d(bot, target) < sPlayerbotAIConfig.sightDistance) &&
           (targetPlayer && IsInGroup_Helper(bot, targetPlayer)) &&
           (!aliveCheck || !target->IsDead()) &&
           (!auraCheck || !ai->HasAura(spell, target));
}

bool SpellTargetTrigger::IsSpellReady()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return spellId && sServerFacade.IsSpellReady(bot, spellId);
}

bool ItemTargetTrigger::IsTargetValid(Unit* target)
{
    if (SpellTargetTrigger::IsTargetValid(target))
    {
        if (itemAuraCheck)
        {
            const uint32 itemId = GetItemId();
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
            if (proto)
            {
                for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                {
                    if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
                    {
                        if (proto->Spells[i].SpellId > 0 && ai->HasAura(proto->Spells[i].SpellId, target))
                        {
                            return false;
                        }
                    }
                }

                return true;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool ItemTargetTrigger::IsSpellReady()
{
    const uint32 itemId = GetItemId();
    if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))
        return false;

    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (proto)
    {
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            {
                if (proto->Spells[i].SpellId > 0)
                {
                    if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId) ||
                        !sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, itemId))
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    return false;
}

bool AtWarTrigger::IsActive()
{
    ReputationMgr& mgr = bot->GetReputationMgr();

    for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
    {
        const FactionEntry* factionEntry = sFactionStore.LookupEntry(id);

        if (!factionEntry)
            continue;

        FactionState const* repState = mgr.GetState(factionEntry);

        if (!repState)
            continue;

        if (!(repState->Flags & FACTION_FLAG_VISIBLE))
            continue;

        if (repState->Flags & FACTION_FLAG_HIDDEN)
            continue;

        if (repState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
            continue;

        if (repState->Flags & FACTION_FLAG_PEACE_FORCED)
            continue;

        if (!(repState->Flags & FACTION_FLAG_AT_WAR))
            continue;

        if (mgr.GetRank(factionEntry) < REP_HOSTILE)
            continue;

        return true;
    }

    return false;
}

// Deferred ready-check confirm is waiting (SOC-S5). Cheap first: config
// gate and anchor read only; no aura scans. The due verdict itself
// (grace/cap/casting) lives in the action's isUseful, so this trigger only
// says "a confirm is held". An anchor held past cap + slack (e.g. through
// combat, when this trigger stays quiet) is a dead check: clear it here so
// no stale confirm goes out when combat ends minutes later.
bool ForceRebuffPendingTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.forceRebuffOnReadyCheck || bot->IsInCombat())
        return false;

    time_t anchor = context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Get();
    if (anchor == time_t(0))
        return false;

    if (time(0) - anchor > ai::ReadyRebuffCapSec() + ai::ReadyRebuffExpirySlackSec())
    {
        context->GetValue<time_t>("manual time", ai::ReadyRebuffAnchorKey())->Set(time_t(0));
        return false;
    }
    return true;
}

// One-shot per pet identity (E01): ported from mod-playerbots NewPetTrigger,
// adapted to this core (only GetPet() exists — no GetGuardianPet, grep
// verified). Fires once per main-pet GUID change; an empty slot resets the
// latch so the next summon re-fires exactly once.
bool NewPetTrigger::IsActive()
{
    ObjectGuid current;
    if (Pet* pet = bot->GetPet())
        current = pet->getObjectGuid();

    if (current != lastPetGuid)
    {
        lastPetGuid = current;
        fired = false;
    }

    if (!current.IsEmpty() && !fired)
    {
        fired = true;
        return true;
    }

    return false;
}

bool PetAttackTrigger::IsActive()
{
    Pet* pet = bot->GetPet();
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!AttackAction::CanPetAttack(ai, pet, target))
        return false;

    if (pet->GetVictim() == target && pet->GetCharmInfo() && pet->GetCharmInfo()->IsCommandAttack())
        return false;

    return true;
}
