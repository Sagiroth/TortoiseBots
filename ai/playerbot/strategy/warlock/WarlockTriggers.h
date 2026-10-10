#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    DEFLECT_TRIGGER(ShadowWardTrigger, "shadow ward");

	class DemonArmorTrigger : public BuffTrigger
	{
	public:
		DemonArmorTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "demon armor") {}
		virtual bool IsActive() override;
	};

    class SpellstoneTrigger : public BuffTrigger
    {
    public:
        SpellstoneTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "spellstone") {}
        virtual bool IsActive() override;
    };

    // Swim-gated water breathing (WAR-6, donor parity, shaman WaterBreathing
    // idiom): self + party rows fire only while the bot swims, so the buff
    // lands where the water is instead of on every buff tick on land.
    class UnendingBreathTrigger : public BuffTrigger
    {
    public:
        UnendingBreathTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "unending breath", 5) {}
        virtual bool IsActive() override;
    };

    class UnendingBreathOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        UnendingBreathOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "unending breath", 2) {}
        virtual bool IsActive() override;
    };

    class NoCurseTrigger : public Trigger
    {
    public:
        NoCurseTrigger(PlayerbotAI* ai) : Trigger(ai, "no curse") {}
        bool IsActive() override;

    private:
        std::string GetTargetName() override { return "current target"; }
    };

    class NoCurseOnAttackerTrigger : public Trigger
    {
    public:
        NoCurseOnAttackerTrigger(PlayerbotAI* ai) : Trigger(ai, "no curse on attacker") {}
        bool IsActive() override;
    };

    DEBUFF_TRIGGER_A(CorruptionTrigger, "corruption");
    class SiphonLifeTrigger : public DebuffTrigger
    {
    public:
        SiphonLifeTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "siphon life") {}
        bool IsActive() override;
    };

    INTERRUPT_TRIGGER(DeathCoilInterruptTrigger, "death coil");
    INTERRUPT_HEALER_TRIGGER(DeathCoilInterruptTHealerTrigger, "death coil");
    SNARE_TRIGGER(DeathCoilSnareTrigger, "death coil");
    SNARE_TRIGGER(CurseOfExhaustionSnareTrigger, "curse of exhaustion");

    class CorruptionOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CorruptionOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "corruption") {}
        bool IsActive() override;
    };

    class CurseOfAgonyTrigger : public DebuffTrigger
    {
    public:
        CurseOfAgonyTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "curse of agony") {}
        bool IsActive() override;
    };

    class CurseOfAgonyOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfAgonyOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of agony") {}
    };

    DEBUFF_TRIGGER(CurseOfDoomTrigger, "curse of doom");

    class CurseOfDoomOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfDoomOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of doom") {}
    };

    DEBUFF_TRIGGER(CurseOfTheElementsTrigger, "curse of the elements");

    class CurseOfTheElementsOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfTheElementsOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of the elements") {}
    };

    DEBUFF_TRIGGER(CurseOfRecklessnessTrigger, "curse of recklessness");

    class CurseOfRecklessnessOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfRecklessnessOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of recklessness") {}
    };

    DEBUFF_TRIGGER(CurseOfWeaknessTrigger, "curse of weakness");

    class CurseOfWeaknessOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfWeaknessOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of weakness") {}
    };

    DEBUFF_TRIGGER(CurseOfTonguesTrigger, "curse of tongues");

    class CurseOfTonguesOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfTonguesOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of tongues") {}
    };

    DEBUFF_TRIGGER(CurseOfShadowTrigger, "curse of shadow");

    class CurseOfShadowOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        CurseOfShadowOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "curse of shadow") {}
    };

    class SiphonLifeOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        SiphonLifeOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "siphon life") {}
    };

    DEBUFF_TRIGGER(ImmolateTrigger, "immolate");

    class ImmolateOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        ImmolateOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "immolate") {}
    };

    class ShadowTranceTrigger : public HasAuraTrigger
    {
    public:
        ShadowTranceTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "shadow trance") {}
    };

    class BanishTrigger : public HasCcTargetTrigger
    {
    public:
        BanishTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "banish") {}
    };

    // PET-2: succubus Seduction rides the same RTI CC flow as banish/fear
    // (mark gating, auto-cc opt-in, spell-ready check). The succubus +
    // humanoid gates live in the action; the trigger stays donor-shaped.
    class SeductionTrigger : public HasCcTargetTrigger
    {
    public:
        SeductionTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "seduction") {}
    };

    class WarlockConjuredItemTrigger : public ItemCountTrigger
    {
    public:
        WarlockConjuredItemTrigger(PlayerbotAI* ai, std::string item) : ItemCountTrigger(ai, item, 1) {}

        virtual bool IsActive() override { return ItemCountTrigger::IsActive(); }
    };

    class HasSpellstoneTrigger : public WarlockConjuredItemTrigger
    {
    public:
        HasSpellstoneTrigger(PlayerbotAI* ai) : WarlockConjuredItemTrigger(ai, "spellstone") {}
    };

    class HasFirestoneTrigger : public WarlockConjuredItemTrigger
    {
    public:
        HasFirestoneTrigger(PlayerbotAI* ai) : WarlockConjuredItemTrigger(ai, "firestone") {}
    };

    class HasHealthstoneTrigger : public WarlockConjuredItemTrigger
    {
    public:
        HasHealthstoneTrigger(PlayerbotAI* ai) : WarlockConjuredItemTrigger(ai, "healthstone") {}
    };

    class HasSoulstoneTrigger : public WarlockConjuredItemTrigger
    {
    public:
        HasSoulstoneTrigger(PlayerbotAI* ai) : WarlockConjuredItemTrigger(ai, "soulstone") {}
    };

    class FearTrigger : public HasCcTargetTrigger
    {
    public:
        FearTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "fear") {}
        virtual bool IsActive() override;
    };

    class AmplifyCurseTrigger : public BuffTrigger
    {
    public:
        AmplifyCurseTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "amplify curse") {}
    };

    class InfernoTrigger : public BuffTrigger
    {
    public:
        InfernoTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "inferno", 10) {}
        virtual bool IsActive() override;
    };

    class LifeTapTrigger : public Trigger
    {
    public:
        LifeTapTrigger(PlayerbotAI* ai) : Trigger(ai, "life tap", 2) {}
        bool IsActive() override;
    };

    class DrainSoulTrigger : public Trigger
    {
    public:
        DrainSoulTrigger(PlayerbotAI* ai) : Trigger(ai, "drain soul") {}
        bool IsActive() override;
    };

    class NoSoulShardTrigger : public Trigger
    {
    public:
        NoSoulShardTrigger(PlayerbotAI* ai) : Trigger(ai, "no soul shard") {}
        // Shards are real for every bot (organic harvest, never seeded), so
        // the zero-shard upkeep must run under the item cheat too. The
        // create action itself gates on a known trainable spell.
        bool IsActive() override { return bot->GetItemCount(6265) == 0; }
    };
    class TooManySoulShardsTrigger : public Trigger
    {
    public:
        TooManySoulShardsTrigger(PlayerbotAI* ai) : Trigger(ai, "too many soul shards") {}
        bool IsActive() override { return !bot->IsInCombat() && bot->GetItemCount(6265) > 5; }
    };

    class FearPvpTrigger : public Trigger
    {
    public:
        FearPvpTrigger(PlayerbotAI* ai) : Trigger(ai, "fear pvp") {}
        bool IsActive() override;
    };

    class ConflagrateTrigger : public Trigger
    {
    public:
        ConflagrateTrigger(PlayerbotAI* ai) : Trigger(ai, "conflagrate") {}
        bool IsActive() override;
    };

    class DemonicSacrificeTrigger : public Trigger
    {
    public:
        DemonicSacrificeTrigger(PlayerbotAI* ai) : Trigger(ai, "demonic sacrifice", 5) {}
        bool IsActive() override;
    };

    class SoulLinkTrigger : public Trigger
    {
    public:
        SoulLinkTrigger(PlayerbotAI* ai) : Trigger(ai, "soul link", 5) {}
        bool IsActive() override;
    };

    class NoSpecificPetTrigger : public Trigger
    {
    public:
        NoSpecificPetTrigger(PlayerbotAI* ai, std::string name, uint32 petEntry) : Trigger(ai, name, 5), entry(petEntry) {}
        bool IsActive() override;

    private:
        uint32 entry;
    };

    class NoImpTrigger : public NoSpecificPetTrigger
    {
    public:
        NoImpTrigger(PlayerbotAI* ai) : NoSpecificPetTrigger(ai, "no imp", 416) {}
    };

    class NoVoidwalkerTrigger : public NoSpecificPetTrigger
    {
    public:
        NoVoidwalkerTrigger(PlayerbotAI* ai) : NoSpecificPetTrigger(ai, "no voidwalker", 1860) {}
    };

    class NoSuccubusTrigger : public NoSpecificPetTrigger
    {
    public:
        NoSuccubusTrigger(PlayerbotAI* ai) : NoSpecificPetTrigger(ai, "no succubus", 1863) {}
    };

    class NoFelhunterTrigger : public NoSpecificPetTrigger
    {
    public:
        NoFelhunterTrigger(PlayerbotAI* ai) : NoSpecificPetTrigger(ai, "no felhunter", 417) {}
    };

    // Solo default-pet choice (pool bots only): fires while the bot knows
    // Summon Voidwalker but runs a lesser demon. Ported from mod-playerbots
    // WrongPetTrigger, collapsed to the one transition this realm has ranks
    // for (Imp -> Voidwalker at 10); owned/hired bots are excluded so a
    // player's manual pet choice is never overridden.
    class WrongPetTrigger : public Trigger
    {
    public:
        WrongPetTrigger(PlayerbotAI* ai) : Trigger(ai, "wrong pet", 5) {}
        bool IsActive() override;
    };

    class SpellLockTrigger : public InterruptSpellTrigger
    {
    public:
        SpellLockTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "spell lock") {}

        bool IsActive() override
        {
            return InterruptSpellTrigger::IsActive();
        }
    };

    class DevourMagicPurgeTrigger : public TargetAuraDispelTrigger
    {
    public:
        DevourMagicPurgeTrigger(PlayerbotAI* ai) : TargetAuraDispelTrigger(ai, "devour magic", DISPEL_MAGIC) {}
        bool IsActive() override
        {
            // Cheap-first: no Felhunter, no aura scan and no queue spam —
            // the actions would discard as USELESS anyway.
            Unit* pet = AI_VALUE(Unit*, "pet target");
            return pet && pet->GetEntry() == 417 && TargetAuraDispelTrigger::IsActive();
        }
    };

    class DevourMagicCleanseTrigger : public PartyMemberNeedCureTrigger
    {
    public:
        DevourMagicCleanseTrigger(PlayerbotAI* ai) : PartyMemberNeedCureTrigger(ai, "devour magic", DISPEL_MAGIC) {}
        bool IsActive() override
        {
            // Same cheap-first gate: the party-wide dispel scan only runs
            // while a Felhunter is actually out.
            Unit* pet = AI_VALUE(Unit*, "pet target");
            return pet && pet->GetEntry() == 417 && PartyMemberNeedCureTrigger::IsActive();
        }
    };

    class SpellLockEnemyHealerTrigger : public InterruptEnemyHealerTrigger
    {
    public:
        SpellLockEnemyHealerTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "spell lock") {}
    };

    class SoulstoneTrigger : public ItemTargetTrigger
    {
    public:
        SoulstoneTrigger(PlayerbotAI* ai) : ItemTargetTrigger(ai, "revive targets", true, true) {}
        std::string GetTargetName() override { return "party member to soulstone"; }
        uint32 GetItemId() override;
    };

    class RainOfFireChannelCheckTrigger : public Trigger
    {
    public:
        RainOfFireChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "rain of fire channel check") {}
        bool IsActive() override;
    };

    class DarkHarvestTrigger : public SpellCanBeCastedTrigger
    {
    public:
        DarkHarvestTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "dark harvest") {}
        bool IsActive() override;
    };

    class DarkHarvestChannelCheckTrigger : public Trigger
    {
    public:
        DarkHarvestChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "dark harvest channel check") {}
        bool IsActive() override;
    };

    class PowerOverwhelmingTrigger : public SpellCanBeCastedTrigger
    {
    public:
        PowerOverwhelmingTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "power overwhelming") {}
        bool IsActive() override;
    };

    // PET-6: demon below half while the owner can afford the drain.
    class HealthFunnelTrigger : public Trigger
    {
    public:
        HealthFunnelTrigger(PlayerbotAI* ai) : Trigger(ai, "health funnel") {}
        bool IsActive() override;
    };
}
