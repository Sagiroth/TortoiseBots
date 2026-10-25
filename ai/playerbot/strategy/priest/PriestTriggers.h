#pragma once

#include "playerbot/strategy/triggers/GenericTriggers.h"
#include "playerbot/strategy/triggers/HealthTriggers.h"
#include "playerbot/policy/heal/HealPacePolicy.h"
#include "playerbot/RaidTankHealing.h"

namespace ai
{
    DEBUFF_TRIGGER(HolyFireTrigger, "holy fire");
    DEBUFF_TRIGGER(PowerWordPainTrigger, "shadow word: pain");
    DEBUFF_ENEMY_TRIGGER(PowerWordPainOnAttackerTrigger, "shadow word: pain");
    DEBUFF_ENEMY_TRIGGER(DevouringPlagueOnAttackerTrigger, "devouring plague");
    DEBUFF_TRIGGER(VampiricEmbraceTrigger, "vampiric embrace");
    CURE_TRIGGER(DispelMagicTrigger, "dispel magic", DISPEL_MAGIC);
    CURE_PARTY_TRIGGER(DispelMagicPartyMemberTrigger, "dispel magic", DISPEL_MAGIC);
    CURE_TRIGGER(CureDiseaseTrigger, "cure disease", DISPEL_DISEASE);
    CURE_PARTY_TRIGGER(PartyMemberCureDiseaseTrigger, "cure disease", DISPEL_DISEASE);
    BUFF_TRIGGER_A(InnerFireTrigger, "inner fire");
    BUFF_TRIGGER_A(ShadowformTrigger, "shadowform");
    BUFF_TRIGGER(InnerFocusTrigger, "inner focus");

    // mod-playerbots parity (PRI-7): Inner Focus pre-cast when mana is low
    // and a heal target exists. Self-gated on the trained spell, the
    // cooldown, medium-or-lower mana and a medium-or-worse heal target, so
    // untalented bots and cooling-down bots never queue an impossible cast.
    class InnerFocusForHealTrigger : public Trigger
    {
    public:
        InnerFocusForHealTrigger(PlayerbotAI* ai) : Trigger(ai, "inner focus for heal") {}
        bool IsActive() override;
    };

    // Owner-approved behaviour (2026-10-14): while the tank does not hold the
    // whole pack - two or more attackers on someone other than a tank - a
    // party healer takes Flash Heal over Greater Heal. The long cast lost the
    // race against the damage and drew threat from every loose mob (ZF hire
    // run: 3555 -> 1354 mana in 38 s, healer top threat 28.5 s).
    bool PriestPackLoose(PlayerbotAI* ai);

    class PartyMemberLowHealthLoosePackTrigger : public PartyMemberLowHealthTrigger
    {
    public:
        PartyMemberLowHealthLoosePackTrigger(PlayerbotAI* ai) : PartyMemberLowHealthTrigger(ai, "party member low health loose pack") {}
        bool IsActive() override { return PartyMemberLowHealthTrigger::IsActive() && PriestPackLoose(ai); }
    };

    class PartyMemberMediumHealthLoosePackTrigger : public PartyMemberLowHealthTrigger
    {
    public:
        PartyMemberMediumHealthLoosePackTrigger(PlayerbotAI* ai) :
            PartyMemberLowHealthTrigger(ai, "party member medium health loose pack", sPlayerbotAIConfig.mediumHealth, sPlayerbotAIConfig.lowHealth) {}
        bool IsActive() override { return PartyMemberLowHealthTrigger::IsActive() && PriestPackLoose(ai); }
    };

    class LowHealthLoosePackTrigger : public LowHealthTrigger
    {
    public:
        LowHealthLoosePackTrigger(PlayerbotAI* ai) : LowHealthTrigger(ai, "low health loose pack") {}
        bool IsActive() override { return LowHealthTrigger::IsActive() && PriestPackLoose(ai); }
    };

    // Direct-heal pick by the pace of the damage (HealPacePolicy.h), one
    // trigger across the low and medium bands so the pick cannot downgrade
    // from a fast heal to a slow one as the bar crosses the band line. The
    // "incoming damage" value - the same signal the preheal gate reads -
    // is one melee swing of each attacker in reach of the target (melee
    // autos only: spells, ranged and dots are not counted). With enough
    // healer mana, damage heavy enough to push the bar into the critical
    // band (medium: out of the band entirely) before a 3 s Heal/Greater
    // Heal lands takes Flash Heal; slow or stable damage (or a healer low
    // on mana) keeps the efficient rows, leaving Flash Heal for the
    // critical-prediction case. The loose-pack rows below outrank the
    // efficient rows, so a pack off the tank still takes Flash Heal first.
    class PartyMemberFastDamageTrigger : public PartyMemberLowHealthTrigger
    {
    public:
        PartyMemberFastDamageTrigger(PlayerbotAI* ai) : PartyMemberLowHealthTrigger(ai, "party member fast damage",
            sPlayerbotAIConfig.mediumHealth, sPlayerbotAIConfig.criticalHealth) {}
        // Band gate on the CURRENT health, not the preheal-predicted one the
        // base trigger checks: heavy damage must not push a member out of
        // the band here - that would switch the pace pick off exactly when
        // the damage is fastest.
        bool IsActive() override
        {
            Unit* target = GetTarget();
            if (!target || AI_VALUE2(bool, "dead", GetTargetName()))
                return false;
            float const healthPercent = GetValue();
            if (healthPercent >= sPlayerbotAIConfig.mediumHealth || healthPercent < sPlayerbotAIConfig.criticalHealth)
                return false;
            if (!target->GetMaxHealth())
                return false;
            uint32 const incDmg = AI_VALUE2(uint32, "incoming damage", GetTargetName());
            // Heal/Greater Heal 3 s - fixed base cast.
            HealPaceState const state{
                healthPercent,
                float(incDmg) / float(target->GetMaxHealth()) * 100.0f,
                3.0f,
                float(AI_VALUE2(uint8, "mana", "self target")),
                float(sPlayerbotAIConfig.mediumMana),
                float(sPlayerbotAIConfig.lowHealth),
                float(sPlayerbotAIConfig.criticalHealth)};
            return PickPacedHeal(state) == HealPaceChoice::Flash;
        }
    };

    // The capstone's cooldown runs all but a sliver of the fight while the
    // aura is missing, so a plain buff trigger would arm the row (and refuse
    // the cast) every tick; the balance gate stays the row's own business.
    class AscendanceTrigger : public BuffTrigger
    {
    public:
        AscendanceTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "ascendance") {}
        bool IsActive() override;
    };

    // Fade's 30 s cooldown keeps the aura missing almost the whole window,
    // so the shed-aggro rows ("fade ready and ...") arm only while the spell
    // is trained and the cooldown spent.
    CD_TRIGGER(FadeReadyTrigger, "fade");
    class ShackleUndeadTrigger : public HasCcTargetTrigger
    {
    public:
        ShackleUndeadTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "shackle undead") {}
        bool IsActive() override
        {
            Unit* target = AI_VALUE2(Unit*, "cc target", "shackle undead");
            return target && !SuppressAutomaticRaidShackle(ai, target, false) && HasCcTargetTrigger::IsActive();
        }
    };
    class PatchwerkSoakerShieldTrigger : public Trigger
    {
    public:
        PatchwerkSoakerShieldTrigger(PlayerbotAI* ai) : Trigger(ai, "patchwerk soaker needs shield") {}
        bool IsActive() override
        {
            uint32 spell = AI_VALUE2(uint32, "spell id", "power word: shield");
            return spell && sServerFacade.IsSpellReady(bot, spell) &&
                AI_VALUE(Unit*, "patchwerk soaker to shield");
        }
    };
    INTERRUPT_TRIGGER(SilenceTrigger, "silence");
    INTERRUPT_HEALER_TRIGGER(SilenceEnemyHealerTrigger, "silence");

    // racials
    DEBUFF_TRIGGER(DevouringPlagueTrigger, "devouring plague");
    BUFF_TRIGGER_A(TouchOfWeaknessTrigger, "touch of weakness");
    DEBUFF_TRIGGER(HexOfWeaknessTrigger, "hex of weakness");
    BUFF_TRIGGER_A(ShadowguardTrigger, "shadowguard");
    DEFLECT_TRIGGER(FeedbackTrigger, "feedback");
    SNARE_TRIGGER(ChastiseTrigger, "chastise");
    DEBUFF_TRIGGER(StarshardsTrigger, "starshards");

    CAN_CAST_TRIGGER(MindBlastTrigger, "mind blast");
    CAN_CAST_TRIGGER(SmiteTrigger, "smite");

    class PowerWordFortitudeOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        PowerWordFortitudeOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "power word: fortitude", 4) {}
        virtual bool IsActive() override { return BuffOnPartyTrigger::IsActive() && !ai->HasAura("prayer of fortitude", GetTarget()); }
    };

    class PowerWordFortitudeTrigger : public BuffTrigger
    {
    public:
        PowerWordFortitudeTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "power word: fortitude", 4) {}
        virtual bool IsActive() override { return BuffTrigger::IsActive() && !ai->HasAura("prayer of fortitude", GetTarget()); }
    };

    class DivineSpiritOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        DivineSpiritOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "divine spirit", 4) {}
        virtual bool IsActive() override { return BuffOnPartyTrigger::IsActive() && !ai->HasAura("prayer of spirit", GetTarget()); }
    };

    class DivineSpiritTrigger : public BuffTrigger
    {
    public:
        DivineSpiritTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "divine spirit", 4) {}
        virtual bool IsActive() override { return BuffTrigger::IsActive() && !ai->HasAura("prayer of spirit", GetTarget()); }
    };

    class ShadowProtectionOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        ShadowProtectionOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "shadow protection", 4) {}
        virtual bool IsActive() override { return BuffOnPartyTrigger::IsActive() && !ai->HasAura("prayer of shadow protection", GetTarget()); }
    };

    class ShadowProtectionTrigger : public BuffTrigger
    {
    public:
        ShadowProtectionTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "shadow protection", 4) {}
        virtual bool IsActive() override { return BuffTrigger::IsActive() && !ai->HasAura("prayer of shadow protection", GetTarget()); }
    };

    class PrayerOfFortitudeOnPartyTrigger : public GreaterBuffOnPartyTrigger
    {
    public:
        PrayerOfFortitudeOnPartyTrigger(PlayerbotAI* ai) : GreaterBuffOnPartyTrigger(ai, "prayer of fortitude", "power word: fortitude", 4) {}
    };

    class PrayerOfSpiritOnPartyTrigger : public GreaterBuffOnPartyTrigger
    {
    public:
        PrayerOfSpiritOnPartyTrigger(PlayerbotAI* ai) : GreaterBuffOnPartyTrigger(ai, "prayer of spirit", "divine spirit", 4) {}
    };

    class PrayerOfShadowProtectionOnPartyTrigger : public GreaterBuffOnPartyTrigger
    {
    public:
        PrayerOfShadowProtectionOnPartyTrigger(PlayerbotAI* ai) : GreaterBuffOnPartyTrigger(ai, "prayer of shadow protection", "shadow protection", 4) {}
    };

    class PowerInfusionTrigger : public SpellTargetTrigger
    {
    public:
        PowerInfusionTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "power infusion", "boost targets", true, true) {}
        std::string GetTargetName() override { return "self target"; }
    };

    // mod-playerbots parity (FearWardOnMainTankTrigger): ward the tank first
    // in combat instead of only whoever lacks the buff. The cooldown guard
    // mirrors the donor: Fear Ward has a 30 s cooldown and the BuffTrigger
    // base never checks it, so without the guard the trigger spins (and the
    // cast fails) every tick while the tank's ward is consumed.
    class FearWardOnTankTrigger : public BuffOnTankTrigger
    {
    public:
        FearWardOnTankTrigger(PlayerbotAI* ai) : BuffOnTankTrigger(ai, "fear ward") {}
        bool IsActive() override;
    };

    class FearWardTrigger : public SpellTargetTrigger
    {
    public:
        FearWardTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "fear ward", "fear ward targets", true, true) {}
        std::string GetTargetName() override { return "self target"; }
    };

    // Tortoise Shadow Priest: Spirit Tap proc-window awareness.
    // Spirit Tap talent 201 (5/5) procs on Mind Blast crit OR target kill,
    // applying buff "Spirit Tap" (spell 15271) for ~30 sec. The buff gives
    // +100% Spirit + 50% mana regen while casting.
    //
    // Important: The Spirit Tap TALENT (15270/15335-38/42003) and the proc
    // BUFF (15271) share the spell name "Spirit Tap" in spell_template.
    // A name-based HasAura("spirit tap") returns true permanently while the
    // talent is learned (passive proc-trigger aura), so we MUST check by
    // spell ID 15271 to detect the actual proc-buff window.
    class SpiritTapBuffTrigger : public Trigger
    {
    public:
        SpiritTapBuffTrigger(PlayerbotAI* ai) : Trigger(ai, "spirit tap buff") {}

        virtual bool IsActive() override
        {
            // 15271 is the Spirit Tap PROC BUFF (school 5 Shadow,
            // durationIndex 8 ~30 sec, +100% Spirit + 50% mana regen
            // while casting). Distinct from the passive talent ranks
            // 15270/15335-15338/42003 which share the spell name.
            return ai->HasAura(15271, bot);
        }
    };

    // Tortoise Shadow Priest: Shadow Weaving stack-aware tracking.
    // Shadow Weaving talent 212 (5/5) applies "Shadow Vulnerability" debuff
    // (spell 15258) on the target via Shadow spell hits. Stacks up to 5x at
    // +3% Shadow damage taken per stack (= +15% at full stacks).
    //
    // Important: The spell name "Shadow Vulnerability" is shared with the
    // Warlock Improved Shadow Bolt debuffs (17793-17803). Name-based check
    // would conflate the two. We check by spell ID 15258 specifically to
    // detect the Priest version (the one our Mind Flay rebuild targets).
    //
    // Trigger fires when the target is MISSING the Priest Shadow Weaving
    // debuff. Used to prioritize Mind Flay / SW:P after target swap to
    // rebuild stacks before Mind Blast burst.
    //
    // Only a priest with the Shadow Weaving talent can apply the debuff, so
    // without it the trigger would fire forever and burn mana on the Mind
    // Flay -> Smite fallback. Spellbook check on the talent ranks, same
    // pattern as the mage Improved Scorch gate.
    class ShadowWeavingMissingTrigger : public Trigger
    {
    public:
        ShadowWeavingMissingTrigger(PlayerbotAI* ai) : Trigger(ai, "shadow weaving missing") {}

        virtual std::string GetTargetName() override { return "current target"; }

        virtual bool IsActive() override
        {
            // Shadow Weaving talent ranks 1-5.
            if (!bot->HasSpell(15257) && !bot->HasSpell(15331) && !bot->HasSpell(15332) &&
                !bot->HasSpell(15333) && !bot->HasSpell(15334))
                return false;

            Unit* target = GetTarget();
            if (!target || !target->IsAlive())
                return false;
            // 15258 is the Priest Shadow Weaving debuff specifically.
            // Not ai->HasAura(): its IsRealAura filter hides another
            // priest's stack on a hostile unit until it reaches 5, so a
            // 1-4 stack read as missing (but 5 did not). Check the target
            // directly: any caster, any stack count.
            return !target->HasAura(15258);
        }
    };

    // A wand is running while a heal the priest's heal rows want, or a nuke
    // its damage rows want, is held back only by the GCD each wand shot
    // renews on Turtle (WandPolicy.h): stop the wand so the priest heals or
    // casts instead of wanding while the party drops or at full mana.
    class PriestWandBlocksCastTrigger : public WandBlocksCastTrigger
    {
    public:
        PriestWandBlocksCastTrigger(PlayerbotAI* ai) : WandBlocksCastTrigger(ai, "wand blocks cast") {}
        bool IsActive() override;
    };
}
