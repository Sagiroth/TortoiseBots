#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    BUFF_TRIGGER(BattleStanceTrigger, "battle stance");
    BUFF_TRIGGER(DefensiveStanceTrigger, "defensive stance");
    BUFF_TRIGGER(BerserkerStanceTrigger, "berserker stance");
    BUFF_TRIGGER(ShieldBlockTrigger, "shield block");
    BUFF_TRIGGER_A(CommandingShoutTrigger, "commanding shout");
    DEBUFF_TRIGGER(RendDebuffTrigger, "rend");
    DEBUFF_TRIGGER(DisarmDebuffTrigger, "disarm");
    DEBUFF_TRIGGER_A(SunderArmorDebuffTrigger, "sunder armor");
    DEBUFF_TRIGGER(DemoralizingShoutDebuffTrigger, "demoralizing shout");
    DEBUFF_TRIGGER(MortalStrikeDebuffTrigger, "mortal strike");
    DEBUFF_ENEMY_TRIGGER(RendDebuffOnAttackerTrigger, "rend");
    CAN_CAST_TRIGGER(DevastateAvailableTrigger, "devastate");
    CAN_CAST_TRIGGER(RevengeAvailableTrigger, "revenge");
    CAN_CAST_TRIGGER(OverpowerAvailableTrigger, "overpower");
    BUFF_TRIGGER(RampageAvailableTrigger, "rampage");
    BUFF_TRIGGER_A(BloodrageBuffTrigger, "bloodrage");
    CAN_CAST_TRIGGER(VictoryRushTrigger, "victory rush");
    HAS_AURA_TRIGGER(SwordAndBoardTrigger, "sword and board");
    SNARE_TRIGGER(ConcussionBlowTrigger, "concussion blow");
    SNARE_TRIGGER(HamstringTrigger, "hamstring");
    SNARE_TRIGGER(MockingBlowTrigger, "mocking blow");
    SNARE_TRIGGER(ThunderClapSnareTrigger, "thunder clap");
    DEBUFF_TRIGGER(ThunderClapTrigger, "thunder clap");
    SNARE_TRIGGER(TauntSnareTrigger, "taunt");
    SNARE_TRIGGER(InterceptSnareTrigger, "intercept");
    CD_TRIGGER(InterceptCanCastTrigger, "intercept");
    SNARE_TRIGGER(ShockwaveSnareTrigger, "shockwave");
    DEBUFF_TRIGGER(ShockwaveTrigger, "shockwave");
    BOOST_TRIGGER(DeathWishTrigger, "death wish");
    BOOST_TRIGGER(RecklessnessTrigger, "recklessness");
    INTERRUPT_HEALER_TRIGGER(ShieldBashInterruptEnemyHealerSpellTrigger, "shield bash");
    INTERRUPT_TRIGGER(ShieldBashInterruptSpellTrigger, "shield bash");
    INTERRUPT_HEALER_TRIGGER(PummelInterruptEnemyHealerSpellTrigger, "pummel");
    INTERRUPT_TRIGGER(PummelInterruptSpellTrigger, "pummel");
    INTERRUPT_HEALER_TRIGGER(InterceptInterruptEnemyHealerSpellTrigger, "intercept");
    INTERRUPT_TRIGGER(InterceptInterruptSpellTrigger, "intercept");
    DEFLECT_TRIGGER(SpellReflectionTrigger, "spell reflection");
    HAS_AURA_TRIGGER(SuddenDeathTrigger, "sudden death");
    HAS_AURA_TRIGGER(SlamInstantTrigger, "slam!");
    HAS_AURA_TRIGGER(TasteForBloodTrigger, "taste for blood");

    class BattleShoutTrigger : public Trigger
    {
    public:
        BattleShoutTrigger(PlayerbotAI* ai) : Trigger(ai, "battle shout") {}

        bool IsActive() override
        {
            static const std::vector<uint32> battleShoutIds = {6673, 5242, 6192, 11549, 11550, 11551, 25289, 2048, 47436};

            for (uint32 id : battleShoutIds)
            {
                if (bot->HasAura(id))
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
            if (spellid && bot->IsSpellReady(spellid))
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
#ifdef MANGOSBOT_ZERO
            return SpellCanBeCastedTrigger::IsActive() && (AI_VALUE2(uint8, "health", "current target") > 20 || ai->IsTank(bot));
#elif MANGOSBOT_ONE
            return SpellCanBeCastedTrigger::IsActive()
                && (AI_VALUE2(uint8, "health", "current target") > 20 || AI_VALUE2(uint8, "rage", "self target") >= 40);
#else
            return SpellCanBeCastedTrigger::IsActive();
#endif

        }
    };

    class WhirlwindTrigger : public SpellCanBeCastedTrigger
    {
    public:
        WhirlwindTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "whirlwind") {}
        bool IsActive() override
        {
#ifdef MANGOSBOT_TWO
            return SpellCanBeCastedTrigger::IsActive();
#else
            return SpellCanBeCastedTrigger::IsActive() && AI_VALUE2(uint8, "health", "current target") > 20;
#endif
        }
    };

    class HeroicStrikeTrigger : public SpellCanBeCastedTrigger
    {
    public:
        HeroicStrikeTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "heroic strike") {}
        bool IsActive() override
        {
#ifdef MANGOSBOT_TWO
            return SpellCanBeCastedTrigger::IsActive();
#else
            bool hasTalents = bot->HasSpell(12294) || bot->HasSpell(21551) || bot->HasSpell(21552) || bot->HasSpell(21553) || bot->HasSpell(25248) || bot->HasSpell(30330) || bot->HasSpell(23881) || bot->HasSpell(23892) || bot->HasSpell(23893) || bot->HasSpell(23894) || bot->HasSpell(25251) || bot->HasSpell(30335);
            if ((hasTalents && AI_VALUE2(uint8, "rage", "self target") > 60) || (!hasTalents && AI_VALUE2(uint8, "rage", "self target") >= 15))
            {
                return SpellCanBeCastedTrigger::IsActive()
                    && (AI_VALUE2(uint8, "health", "current target") > 20 || ai->IsTank(bot));
            }
            return false;
#endif
        }
    };

    class SlamTrigger : public SpellCanBeCastedTrigger
    {
    public:
        SlamTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "slam") {}
    };

    // TurtleWoW Protection Warrior: Defensive Tactics 3/3 = 180% threat
    // retention in non-Defensive stances when wearing a shield.
    //
    // At max rank, Berserker Stance retains MORE than 100% of Defiance's
    // +30% threat boost AND adds +10% damage. The bot wants Berserker
    // Stance whenever survival isn't at risk.
    //
    // This trigger fires when:
    //   - Bot is missing Berserker Stance (BerserkerStanceTrigger semantics)
    //   - Bot is in combat
    //   - Bot HP > 70% (safety threshold)
    //
    // Only used in ProtectionWarriorStrategy — implied "DT talent + shield".
    // If bot doesn't actually have DT or shield, the threat-retention won't
    // hold and the existing "lose aggro" trigger will self-correct via
    // Heroic Throw Taunt.
    class DefensiveTacticsBerserkerStanceTrigger : public BerserkerStanceTrigger
    {
    public:
        DefensiveTacticsBerserkerStanceTrigger(PlayerbotAI* ai) : BerserkerStanceTrigger(ai) {}

        virtual bool IsActive() override
        {
            if (!BerserkerStanceTrigger::IsActive())
                return false;
            if (!sServerFacade.IsInCombat(bot))
                return false;
            return AI_VALUE2(uint8, "health", "self target") > 70;
        }
    };
}
