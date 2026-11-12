#include "playerbot/playerbot.h"
#include "playerbot/BoostReadinessPolicy.h"
#include "PriestTriggers.h"
#include "PriestActions.h"
#include "PriestMultipliers.h"
#include "playerbot/policy/heal/ShadowformHealPolicy.h"
#include "playerbot/strategy/values/HealerCoverage.h"
#include "../../../runtime/policy/class/PriestFadePolicy.h"

using namespace ai;

// BuffTrigger proves the spell is trained and the aura missing; it cannot
// see the capstone's cooldown, and the 5-min cooldown must not fire on the
// first quiet combat tick. Someone must actually need healing.
bool AscendanceTrigger::IsActive()
{
    if (!BuffTrigger::IsActive())
        return false;

    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return ai::HealBoostReady(spellId != 0, spellId && sServerFacade.IsSpellReady(bot, spellId),
        AI_VALUE2(bool, "trigger active", "party member critical health") ||
        AI_VALUE2(bool, "trigger active", "party member low health") ||
        AI_VALUE2(bool, "trigger active", "critical health") ||
        AI_VALUE2(bool, "trigger active", "low health"),
        AI_VALUE2(bool, "trigger active", "medium group heal setting"));
}

bool PrayerOfHealingPartyTrigger::IsActive()
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    unsigned hurt = 0;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !member->IsInWorld() || !group->SameSubGroup(bot, member))
            continue;
        if (!sServerFacade.IsAlive(member) || sServerFacade.GetDistance2d(bot, member) > 30.0f)
            continue;
        float percent = member->GetMaxHealth()
            ? static_cast<float>(member->GetHealth()) / static_cast<float>(member->GetMaxHealth()) * 100.0f
            : 100.0f;
        if (percent <= sPlayerbotAIConfig.mediumHealth)
            ++hurt;
    }
    return PrayerOfHealingWorth(hurt);
}

bool ai::PriestPackLoose(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot || !bot->GetGroup())
        return false;

    // Attackers whose victim is not a tank (a pet, a healer, a DPS).
    uint32 offTank = 0;
    for (ObjectGuid const& guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get())
    {
        Unit* attacker = ai->GetUnit(guid);
        if (!attacker || !attacker->IsAlive())
            continue;
        Unit* victim = attacker->GetVictim();
        if (!victim)
            continue;
        Player* victimPlayer = victim->IsPlayer() ? static_cast<Player*>(victim) : nullptr;
        if (victimPlayer && PlayerbotAI::IsTank(victimPlayer))
            continue;
        if (++offTank >= 2)
            return true;
    }
    return false;
}

bool PriestFadeThreatTrigger::IsActive()
{
    if (!MediumThreatTrigger::IsActive())
        return false;

    TortoiseBots::PriestFadeInputs in;
    in.mediumThreat = true;
    Unit* target = AI_VALUE(Unit*, "current target");
    in.tankHoldsMob = target && AI_VALUE2(float, "tank threat", "current target") > 0.0f;
    return TortoiseBots::ShouldPriestFadeOnThreat(in);
}

bool InnerFireTrigger::IsActive()
{
    return BuffTrigger::IsActive();
}

bool ShadowformTrigger::IsActive()
{
    return ai->HasSpell("shadowform") && !ai->HasAura("shadowform", bot);
}

// Touch of Weakness and Shadowguard overwrite each other: both are priest
// family 6 upkeep buffs with an identical effect signature (effect 6 APPLY_AURA,
// aura 42 PROC_TRIGGER_SPELL, misc 0, item type 0), so the core's generic
// non-stacking rule removes one when the other lands. Stand down while the
// sibling is up, or each upkeep trigger re-fires forever.
bool TouchOfWeaknessTrigger::IsActive()
{
    return BuffTrigger::IsActive() && !ai->HasAura("shadowguard", bot);
}

bool ShadowguardTrigger::IsActive()
{
    return BuffTrigger::IsActive() && !ai->HasAura("touch of weakness", bot);
}
bool FearWardOnTankTrigger::IsActive()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;

    // Explicit orders win: a wardable manual buff target is served by the
    // generic row. With a non-empty list FearWardTrigger checks only manual
    // targets (no self fallback), so mirror that check here: stand down iff
    // a manual target would take the ward. (FearWardTrigger::IsActive is
    // private engine-side, hence the inline mirror of its list branch.)
    const std::list<ObjectGuid>& manualTargets = AI_VALUE(std::list<ObjectGuid>, "buff targets");
    if (!manualTargets.empty())
    {
        FearWardTrigger probe(ai);
        for (const ObjectGuid& guid : manualTargets)
        {
            if (probe.IsTargetValid(ai->GetUnit(guid)))
                return false;
        }
    }

    return BuffOnTankTrigger::IsActive();
}

bool InnerFocusForHealTrigger::IsActive()
{
    // Untalented bots (low level, holy without the 21-point disc talent)
    // must never queue this; BuffTrigger's own HasSpell gate is bypassed
    // because this trigger carries the mana + heal-target conditions.
    if (!ai->HasSpell("inner focus"))
        return false;

    uint32 spellId = AI_VALUE2(uint32, "spell id", "inner focus");
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;

    // Buff already up: nothing to pre-cast.
    if (ai->HasAura("inner focus", bot))
        return false;

    if (!(AI_VALUE2(bool, "has mana", "self target") &&
        AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana))
        return false;

    Unit* healTarget = AI_VALUE(Unit*, "party member to heal");
    if (!healTarget || !healTarget->IsAlive())
        return false;

    return AI_VALUE2(uint8, "health", "party member to heal") < sPlayerbotAIConfig.mediumHealth;
}

bool PriestWandBlocksCastTrigger::IsActive()
{
    if (!IsWanding())
        return false;

    // Heals first: a wanding priest must never sit on a heal its rows want.
    // Every priest heals itself below "low health" (PriestStrategy); holy
    // and discipline heal the party below "medium health" and keep Renew up
    // below "almost full"; offheal heals the party below "medium health".
    bool const healerSpec = ai->ContainsStrategy(STRATEGY_TYPE_HEAL);
    Unit* healTarget = nullptr;
    Unit* renewTarget = nullptr;
    if (AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.lowHealth)
        healTarget = bot;
    else if (healerSpec || ai->HasStrategy("offheal", BotState::BOT_STATE_COMBAT))
    {
        Unit* member = AI_VALUE(Unit*, "party member to heal");
        if (member && member->IsAlive())
        {
            uint8 const health = AI_VALUE2(uint8, "health", "party member to heal");
            if (ShadowformOffhealYields(ai->HasAura("shadowform", bot), healerSpec,
                member->GetHealthPercent() < float(sPlayerbotAIConfig.lowHealth),
                AnotherHealerCoversAlly(ai, member, HealReachLimit(ai))))
            {
                // D1 leaves this Renew to the covering healer, so the wand
                // keeps running: stopping it would only restart it one swing
                // later. The self branch above is unchanged.
            }
            else if (health < sPlayerbotAIConfig.mediumHealth)
                healTarget = member;
            // Between medium and almost full only the "party member almost
            // full health -> renew on party" row fires, so probe Renew there:
            // a castable Flash/Lesser Heal says nothing about it.
            else if (healerSpec && health < sPlayerbotAIConfig.almostFullHealth && !ai->HasAura("renew", member))
                renewTarget = member;
        }
    }

    // Heals are probed without the form check: every heal row drops
    // Shadowform first ("remove shadowform" prerequisite).
    WandSpellProbe bestHeal = WandSpellProbe::Blocked;
    if (healTarget)
        bestHeal = BetterWandSpellProbe(ProbeSpell("flash heal", healTarget, false),
            ProbeSpell(PriestSlowHealProbeSpell(PriestShouldSuppressLesserHeal(ai)), healTarget, false));
    else if (renewTarget)
        bestHeal = ProbeSpell("renew", renewTarget, false);

    // Nukes: shadow always fights; holy/discipline only through offdps, and
    // there only while "healer should attack" (nobody to heal, mana
    // comfortable) - below that "healer should wand" wands on purpose.
    WandSpellProbe bestNuke = WandSpellProbe::Blocked;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && target->IsAlive())
    {
        bool nukeWanted = !healerSpec;
        if (!nukeWanted && ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT))
        {
            Trigger* shouldAttack = context->GetTrigger("healer should attack");
            nukeWanted = shouldAttack && shouldAttack->IsActive();
        }

        if (nukeWanted)
        {
            // Mind Flay and Smite have no cooldown of their own, so a
            // NOT_READY answer is the GCD. Smite can never be cast in
            // Shadowform (ProbeSpell's form check would refuse it too), so
            // it is not probed there.
            bestNuke = ProbeSpell("mind flay", target);
            if (!ai->HasAura("shadowform", bot))
                bestNuke = BetterWandSpellProbe(bestNuke, ProbeSpell("smite", target));
        }
    }

    // Priests have no raid "has aggro -> shoot" row. Near the tank's threat
    // the "threat" multiplier vetoes the nukes (Smite falls back to the
    // wand); heals are never threat-gated, so a wanted heal still stops it.
    bool const nukeThreatVetoed = bestNuke != WandSpellProbe::Blocked && ThreatVetoesNuke();
    WandState state{true, bestNuke, false, NoManaRowActive(), bestHeal, nukeThreatVetoed};
    // A healer stops its wand as soon as the wand is no longer allowed
    // (HealerMayWand): somebody needs a heal, or the mob left the party.
    // The heal probes above only see heals below "medium health" or a
    // missing Renew, and a wand renews the GCD that every heal row needs.
    state.healerMustStop = healerSpec && !HealerWandAllowed(ai, target);
    return ShouldStopWandToCast(state);
}

bool PriestHealerWandNotAllowedTrigger::IsActive()
{
    Spell* autoRepeat = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
    if (!autoRepeat || !autoRepeat->m_spellInfo || autoRepeat->m_spellInfo->Category != WAND_SHOOT_CATEGORY)
        return false;
    if (!IsGroupedHealer(ai))
        return false;
    return !HealerWandAllowed(ai, AI_VALUE(Unit*, "current target"));
}
