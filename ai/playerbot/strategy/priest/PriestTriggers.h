#pragma once

#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    DEBUFF_TRIGGER(HolyFireTrigger, "holy fire");
    DEBUFF_TRIGGER(PowerWordPainTrigger, "shadow word: pain");
    DEBUFF_ENEMY_TRIGGER(PowerWordPainOnAttackerTrigger, "shadow word: pain");
    DEBUFF_TRIGGER(VampiricTouchTrigger, "vampiric touch");
    DEBUFF_ENEMY_TRIGGER(VampiricTouchOnAttackerTrigger, "vampiric touch");
    DEBUFF_TRIGGER(VampiricEmbraceTrigger, "vampiric embrace");
    CURE_TRIGGER(DispelMagicTrigger, "dispel magic", DISPEL_MAGIC);
    CURE_PARTY_TRIGGER(DispelMagicPartyMemberTrigger, "dispel magic", DISPEL_MAGIC);
    CURE_TRIGGER(CureDiseaseTrigger, "cure disease", DISPEL_DISEASE);
    CURE_PARTY_TRIGGER(PartyMemberCureDiseaseTrigger, "cure disease", DISPEL_DISEASE);
    BUFF_TRIGGER_A(InnerFireTrigger, "inner fire");
    BUFF_TRIGGER_A(ShadowformTrigger, "shadowform");
    BUFF_TRIGGER(InnerFocusTrigger, "inner focus");
    CC_TRIGGER(ShackleUndeadTrigger, "shackle undead");
    INTERRUPT_TRIGGER(SilenceTrigger, "silence");
    INTERRUPT_HEALER_TRIGGER(SilenceEnemyHealerTrigger, "silence");

    // racials
    DEBUFF_TRIGGER(DevouringPlagueTrigger, "devouring plague");
    BUFF_TRIGGER(TouchOfWeaknessTrigger, "touch of weakness");
    DEBUFF_TRIGGER(HexOfWeaknessTrigger, "hex of weakness");
    BUFF_TRIGGER(ShadowguardTrigger, "shadowguard");
    DEFLECT_TRIGGER(FeedbackTrigger, "feedback");
    SNARE_TRIGGER(ChastiseTrigger, "chastise");
    DEBUFF_TRIGGER(StarshardsTrigger, "starshards");

    BOOST_TRIGGER_A(ShadowfiendTrigger, "shadowfiend");
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

    class BindingHealTrigger : public PartyMemberLowHealthTrigger 
    {
    public:
        BindingHealTrigger(PlayerbotAI* ai) : PartyMemberLowHealthTrigger(ai, "binding heal", sPlayerbotAIConfig.lowHealth, 0) {}

        virtual bool IsActive() override
        {
            return PartyMemberLowHealthTrigger::IsActive() && AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.mediumHealth;
        }
    };

    class PowerInfusionTrigger : public SpellTargetTrigger
    {
    public:
        PowerInfusionTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "power infusion", "boost targets", true, true) {}
        std::string GetTargetName() override { return "self target"; }
    };

    class FearWardTrigger : public SpellTargetTrigger
    {
    public:
        FearWardTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "fear ward", "buff targets", true, true) {}
        std::string GetTargetName() override { return "self target"; }
    };

    // TurtleWoW Shadow Priest: Spirit Tap proc-window awareness.
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

    // TurtleWoW Shadow Priest: Shadow Weaving stack-aware tracking.
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
    class ShadowWeavingMissingTrigger : public Trigger
    {
    public:
        ShadowWeavingMissingTrigger(PlayerbotAI* ai) : Trigger(ai, "shadow weaving missing") {}

        virtual std::string GetTargetName() override { return "current target"; }

        virtual bool IsActive() override
        {
            Unit* target = GetTarget();
            if (!target || !target->IsAlive())
                return false;
            // 15258 is the Priest Shadow Weaving debuff specifically.
            return !ai->HasAura(15258, target);
        }
    };
}
