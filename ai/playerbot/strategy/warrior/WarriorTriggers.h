#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"
#include "playerbot/BattleShoutPolicy.h"

namespace ai
{
    BUFF_TRIGGER(BattleStanceTrigger, "battle stance");
    BUFF_TRIGGER(DefensiveStanceTrigger, "defensive stance");
    BUFF_TRIGGER(BerserkerStanceTrigger, "berserker stance");
    BUFF_TRIGGER(ShieldBlockTrigger, "shield block");
    DEBUFF_TRIGGER(RendDebuffTrigger, "rend");
    DEBUFF_TRIGGER(DisarmDebuffTrigger, "disarm");
    DEBUFF_TRIGGER_A(SunderArmorDebuffTrigger, "sunder armor");
    DEBUFF_TRIGGER(DemoralizingShoutDebuffTrigger, "demoralizing shout");
    DEBUFF_TRIGGER(MortalStrikeDebuffTrigger, "mortal strike");
    DEBUFF_ENEMY_TRIGGER(RendDebuffOnAttackerTrigger, "rend");
    CAN_CAST_TRIGGER(RevengeAvailableTrigger, "revenge");
    CAN_CAST_TRIGGER(OverpowerAvailableTrigger, "overpower");
    BUFF_TRIGGER_A(BloodrageBuffTrigger, "bloodrage");
    // Turtle Improved Shield Slam proc (51596/51597, +35%/+70% slam damage
    // on melee-ability hit): same slot as the donor's Sword-and-Board row,
    // but matched by spell id, not name — the permanent talent (51598/51599)
    // shares the "Improved Shield Slam" name and would keep a name trigger
    // active forever. Proc aura is 1 charge (procCharges=1), so firing slam
    // immediately consumes the bonus before it expires.
    class ImprovedShieldSlamProcTrigger : public Trigger
    {
    public:
        ImprovedShieldSlamProcTrigger(PlayerbotAI* ai) : Trigger(ai, "improved shield slam proc") {}

        bool IsActive() override
        {
            return ai->HasAura(51596, bot) || ai->HasAura(51597, bot);
        }
    };
    SNARE_TRIGGER(ConcussionBlowTrigger, "concussion blow");
    SNARE_TRIGGER(HamstringTrigger, "hamstring");
    SNARE_TRIGGER(MockingBlowTrigger, "mocking blow");
    SNARE_TRIGGER(ThunderClapSnareTrigger, "thunder clap");
    DEBUFF_TRIGGER(ThunderClapTrigger, "thunder clap");
    SNARE_TRIGGER(TauntSnareTrigger, "taunt");
    SNARE_TRIGGER(InterceptSnareTrigger, "intercept");
    CD_TRIGGER(InterceptCanCastTrigger, "intercept");
    BOOST_TRIGGER(DeathWishTrigger, "death wish");
    BOOST_TRIGGER(RecklessnessTrigger, "recklessness");
    INTERRUPT_HEALER_TRIGGER(ShieldBashInterruptEnemyHealerSpellTrigger, "shield bash");
    INTERRUPT_TRIGGER(ShieldBashInterruptSpellTrigger, "shield bash");
    INTERRUPT_HEALER_TRIGGER(PummelInterruptEnemyHealerSpellTrigger, "pummel");
    INTERRUPT_TRIGGER(PummelInterruptSpellTrigger, "pummel");
    INTERRUPT_HEALER_TRIGGER(InterceptInterruptEnemyHealerSpellTrigger, "intercept");
    INTERRUPT_TRIGGER(InterceptInterruptSpellTrigger, "intercept");
    HAS_AURA_TRIGGER(SuddenDeathTrigger, "sudden death");
    HAS_AURA_TRIGGER(SlamInstantTrigger, "slam!");
    HAS_AURA_TRIGGER(TasteForBloodTrigger, "taste for blood");

    class BattleShoutTrigger : public Trigger
    {
    public:
        BattleShoutTrigger(PlayerbotAI* ai) : Trigger(ai, "battle shout") {}

        bool IsActive() override
        {
            static const std::vector<uint32> battleShoutIds = {6673, 5242, 6192, 11549, 11550, 11551, 25289, 2048};

            for (uint32 id : battleShoutIds)
            {
                if (bot->HasAura(id))
                    return false;
            }

            // Skip Battle Shout when a stronger Blessing of Might is up (donor
            // BattleShoutTrigger compares AP values). Tables + rule live in
            // BattleShoutPolicy.h (unit-tested). Own AP scaled by Improved
            // Battle Shout talent (donor COMMANDING_PRESENCE_RANKS are these
            // same Vanilla ids); highest learned rank wins. Spellbook check
            // (not aura): passive talent auras may not be visible.
            static const uint32 impBattleShoutRanks[] = { 12318, 12857, 12858, 12860, 12861 };
            float talentBonus = 0.0f;
            for (int rank = 4; rank >= 0; --rank)
            {
                if (bot->HasSpell(impBattleShoutRanks[rank]))
                {
                    talentBonus = ImprovedBattleShoutBonus(impBattleShoutRanks[rank]);
                    break;
                }
            }
            int32 shoutAp = EffectiveBattleShoutAp(
                BattleShoutAttackPower(AI_VALUE2(uint32, "spell id", "battle shout")), talentBonus);
            if (Aura* might = ai->GetAura("blessing of might", bot))
            {
                if (!ShouldBattleShout(shoutAp, true, BlessingOfMightAttackPower(might->GetSpellProto()->Id)))
                    return false;
            }
            if (Aura* greaterMight = ai->GetAura("greater blessing of might", bot))
            {
                if (!ShouldBattleShout(shoutAp, true, BlessingOfMightAttackPower(greaterMight->GetSpellProto()->Id)))
                    return false;
            }
            return true;
        }
    };

    class BerserkerRageBuffTrigger : public TargetOfFearCastTrigger
    {
    public:
        BerserkerRageBuffTrigger(PlayerbotAI* ai) : TargetOfFearCastTrigger(ai) {}
        bool IsActive() override
        {
            // Check for spell cooldown
            uint32 spellid = AI_VALUE2(uint32, "spell id", "berserker rage");
            if (spellid && sServerFacade.IsSpellReady(bot, spellid))
            {
                return TargetOfFearCastTrigger::IsActive();
            }

            return false;
        }
    };

    class SweepingStrikesTrigger : public SpellCanBeCastedTrigger
    {
    public:
        SweepingStrikesTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "sweeping strikes") {}
        bool IsActive() override
        {
            return SpellCanBeCastedTrigger::IsActive();
        }
    };

    class BloodthirstTrigger : public SpellCanBeCastedTrigger
    {
    public:
        BloodthirstTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "bloodthirst") {}
        bool IsActive() override
        {
            return SpellCanBeCastedTrigger::IsActive() && (AI_VALUE2(uint8, "health", "current target") > 20 || ai->IsTank(bot));

        }
    };

    class WhirlwindTrigger : public SpellCanBeCastedTrigger
    {
    public:
        WhirlwindTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "whirlwind") {}
        bool IsActive() override
        {
            return SpellCanBeCastedTrigger::IsActive() && AI_VALUE2(uint8, "health", "current target") > 20;
        }
    };

    class HeroicStrikeTrigger : public SpellCanBeCastedTrigger
    {
    public:
        HeroicStrikeTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "heroic strike") {}
        bool IsActive() override
        {
            // Rage dump, not a builder: hold heroic strike until 60+ rage so
            // slam/shield-slam/mortal-strike/bloodthirst (all 15-30 rage) fire
            // first (donor TankWarrior gates high-rage the same way). The old
            // 15-rage floor for untalented levelers starved every rage buyer
            // above it in the prot/arms/fury lists.
            if (AI_VALUE2(uint8, "rage", "self target") < 60)
                return false;
            return SpellCanBeCastedTrigger::IsActive()
                && (AI_VALUE2(uint8, "health", "current target") > 20 || ai->IsTank(bot));
        }
    };

    class SlamTrigger : public SpellCanBeCastedTrigger
    {
    public:
        SlamTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "slam") {}
    };

    class MasterStrikeTrigger : public SpellCanBeCastedTrigger
    {
    public:
        MasterStrikeTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "master strike") {}
        bool IsActive() override
        {
            // Core CanCastSpell enforces cooldown/rage/weapon equip. Require a
            // live melee target so the 30s weapon nuke is not wasted, and a
            // main hand so a mismatched/empty slot cannot burn the cooldown.
            if (!SpellCanBeCastedTrigger::IsActive())
                return false;
            Unit* target = GetTarget();
            if (!target || !target->IsAlive())
                return false;
            return bot->GetWeaponForAttack(BASE_ATTACK, true, true) != nullptr;
        }
    };
}
