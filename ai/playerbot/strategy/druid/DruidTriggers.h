#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    class MarkOfTheWildOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        MarkOfTheWildOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "mark of the wild", 4) {}
        virtual bool IsActive() override { return BuffOnPartyTrigger::IsActive() && !ai->HasAura("gift of the wild", GetTarget()); }
    };

    class MarkOfTheWildTrigger : public BuffTrigger
    {
    public:
        MarkOfTheWildTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "mark of the wild", 4) {}
        virtual bool IsActive() override { return BuffTrigger::IsActive() &&  !ai->HasAura("gift of the wild", GetTarget()); }
    };

    class GiftOfTheWildOnPartyTrigger : public GreaterBuffOnPartyTrigger
    {
    public:
        GiftOfTheWildOnPartyTrigger(PlayerbotAI* ai) : GreaterBuffOnPartyTrigger(ai, "gift of the wild", "mark of the wild", 4) {}
    };

    class ThornsOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        ThornsOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "thorns", 4) {}

        virtual bool IsActive() override
        {
            Unit* target = GetTarget();
            if (target && BuffOnPartyTrigger::IsActive() && (!target->IsPlayer() || !ai->IsRanged((Player*)target)))
            {
                // Don't apply thorns if fire shield (conflict) is on the target
                return !ai->HasAura("fire shield", target);
            }

            return false;
        }
    };

    class ThornsTrigger : public BuffTrigger
    {
    public:
        ThornsTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "thorns", 4) {}

        bool IsActive() override
        {
            if (BuffTrigger::IsActive())
            {
                // Don't apply thorns if fire shield (conflict) is on the target
                return !ai->HasAura("fire shield", GetTarget());
            }

            return false;
        }
    };

    class OmenOfClarityTrigger : public BuffTrigger
    {
    public:
        OmenOfClarityTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "omen of clarity") {}
    };

    class RakeTrigger : public DebuffTrigger
    {
    public:
        RakeTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "rake") {}
    };

    class RipTrigger : public NoDebuffAndComboPointsAvailableTrigger
    {
    public:
        RipTrigger(PlayerbotAI* ai, uint8 comboPoints = 3) : NoDebuffAndComboPointsAvailableTrigger(ai, "rip", comboPoints) {}
    };

    class InsectSwarmTrigger : public DebuffTrigger
    {
    public:
        InsectSwarmTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "insect swarm") {}
    };

    class MoonfireTrigger : public DebuffTrigger
    {
    public:
        MoonfireTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "moonfire") {}
        virtual bool IsActive() override { return DebuffTrigger::IsActive() && !GetTarget()->IsRooted(); }
    };
    CAN_CAST_TRIGGER(WrathTrigger, "wrath");

    class FaerieFireTrigger : public DebuffTrigger
    {
    public:
        FaerieFireTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "faerie fire") {}
    };

    class FaerieFireFeralTrigger : public DebuffTrigger
    {
    public:
        FaerieFireFeralTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "faerie fire (feral)") {}
    };

    class BashInterruptSpellTrigger : public InterruptSpellTrigger
    {
    public:
        BashInterruptSpellTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "bash") {}
    };

    class TigersFuryTrigger : public BoostTrigger
    {
    public:
        TigersFuryTrigger(PlayerbotAI* ai) : BoostTrigger(ai, "tiger's fury") {}
    };

    class NaturesGraspTrigger : public BoostTrigger
    {
    public:
        NaturesGraspTrigger(PlayerbotAI* ai) : BoostTrigger(ai, "nature's grasp") {}
    };

    class EntanglingRootsTrigger : public HasCcTargetTrigger
    {
    public:
        EntanglingRootsTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "entangling roots") {}
    };

    class EntanglingRootsKiteTrigger : public DebuffTrigger
    {
    public:
        EntanglingRootsKiteTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "entangling roots") {}
        virtual bool IsActive() override;
    };

    class EntanglingRootsSnareTrigger : public SnareTargetTrigger
    {
    public:
        EntanglingRootsSnareTrigger(PlayerbotAI* ai) : SnareTargetTrigger(ai, "entangling roots", 5) {}
    };

    class HibernateTrigger : public HasCcTargetTrigger
    {
    public:
        HibernateTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "hibernate") {}
    };

    class CurePoisonTrigger : public NeedCureTrigger
    {
    public:
        CurePoisonTrigger(PlayerbotAI* ai) : NeedCureTrigger(ai, "cure poison", DISPEL_POISON) {}
    };

    class PartyMemberCurePoisonTrigger : public PartyMemberNeedCureTrigger
    {
    public:
        PartyMemberCurePoisonTrigger(PlayerbotAI* ai) : PartyMemberNeedCureTrigger(ai, "cure poison", DISPEL_POISON) {}
    };

    CURE_TRIGGER(RemoveCurseTrigger, "remove curse", DISPEL_CURSE);
    CURE_PARTY_TRIGGER(RemoveCurseOnPartyTrigger, "remove curse", DISPEL_CURSE);

    class BearFormTrigger : public BuffTrigger
    {
    public:
        BearFormTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "bear form") {}
        virtual bool IsActive() override { return !ai->HasAnyAuraOf(bot, "bear form", "dire bear form", NULL); }
    };

    class TreeFormTrigger : public BuffTrigger
    {
    public:
        TreeFormTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "tree of life") {}
        virtual bool IsActive() override { return !ai->HasAura("tree of life", bot); }
    };

    class MoonkinFormTrigger : public BuffTrigger
    {
    public:
        MoonkinFormTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "moonkin form") {}
        virtual bool IsActive() override { return !ai->HasAura("moonkin form", bot); }
    };

    class CatFormTrigger : public BuffTrigger
    {
    public:
        CatFormTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "cat form") {}
        virtual bool IsActive() override { return !ai->HasAura("cat form", bot); }
    };

    // State signal (not a buff request): true while the bot sits in Bear,
    // Dire Bear or Cat form. The leveling kit uses it to stand down: a
    // shapeshifted 10+ druid already runs a feral kit, so the caster
    // wrath/moonfire/heal nodes must not outbid the form rotation.
    class InFeralFormTrigger : public Trigger
    {
    public:
        InFeralFormTrigger(PlayerbotAI* ai) : Trigger(ai, "in feral form") {}
        bool IsActive() override;
    };

    // Tortoise Balance redesign: Eclipse capstone (talent 320, spell 51444).
    // Wrath crit → "Arcane Eclipse" buff (spell 51443) → boosts Arcane damage
    //   → bot pivots to spam Starfire during the ~10 sec window.
    // Starfire crit → "Nature Eclipse" buff (spell 51442) → boosts Nature damage
    //   → bot pivots to spam Wrath during the ~10 sec window.
    // Each Eclipse has a 30-sec internal cooldown; only one active at a time.
    // Spell names in spell_template:
    //   51442 "Nature Eclipse"  (school 3 Nature, durationIndex 8 ~30 sec)
    //   51443 "Arcane Eclipse"  (school 6 Arcane, durationIndex 8 ~30 sec)
    class HasArcaneEclipseTrigger : public HasAuraTrigger
    {
    public:
        HasArcaneEclipseTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "arcane eclipse") {}
    };

    class HasNatureEclipseTrigger : public HasAuraTrigger
    {
    public:
        HasNatureEclipseTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "nature eclipse") {}
    };

    class BashInterruptEnemyHealerSpellTrigger : public InterruptEnemyHealerTrigger
    {
    public:
        BashInterruptEnemyHealerSpellTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "bash") {}
    };

    class NaturesSwiftnessTrigger : public BuffTrigger
    {
    public:
        NaturesSwiftnessTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "nature's swiftness") {}
    };

    // mod-playerbots parity (DRU-2): true while the Nature's Swiftness buff
    // sits on the bot, so the emergency instant-Healing-Touch row can spend
    // it immediately. Mirrors shaman AncestralSwiftnessAuraTrigger.
    class NaturesSwiftnessActiveTrigger : public HasAuraTrigger
    {
    public:
        NaturesSwiftnessActiveTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "nature's swiftness") {}
    };

    class EnrageTrigger : public BuffTrigger
    {
    public:
        EnrageTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "enrage") {}

    private:
        bool IsActive() override
        {
            return AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth &&
                   AI_VALUE2(uint8, "rage", "self target") < 20;
        }
    };

    class InStealthTrigger : public HasAuraTrigger
    {
    public:
        InStealthTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "prowl") {}
    };

    class NoStealthTrigger : public HasNoAuraTrigger
    {
    public:
        NoStealthTrigger(PlayerbotAI* ai) : HasNoAuraTrigger(ai, "prowl")
        {
            checkInterval = 2;
        }
    };

    class DruidUnstealthTrigger : public BuffTrigger
    {
    public:
        DruidUnstealthTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "prowl", 2) {}

        bool IsActive() override
        {
            if (ai->HasAura("prowl", bot) && !bot->InBattleGround())
            {
                if (!AI_VALUE(bool, "has attackers") && !AI_VALUE(bool, "has enemy player targets"))
                {
                    if (AI_VALUE2(bool, "moving", "self target"))
                    {
                        if (ai->GetMaster())
                        {
                            return sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "master target"), 10.0f);
                        }
                        else
                        {
                            return true;
                        }
                    }
                }
            }

            return false;
        }
    };

    class StealthTrigger : public Trigger
    {
    public:
        StealthTrigger(PlayerbotAI* ai) : Trigger(ai, "prowl") {}

        bool IsActive() override
        {
            if (ai->HasAura("prowl", bot) || sServerFacade.IsInCombat(bot) || !sServerFacade.IsSpellReady(bot, 5215))
            {
                return false;
            }

            Unit* target = AI_VALUE(Unit*, "enemy player target");
            if (!target)
            {
                target = AI_VALUE(Unit*, "grind target");
            }
            if (!target)
            {
                target = AI_VALUE(Unit*, "dps target");
            }
            if (!target)
            {
                return false;
            }

            float distance = 30.0f;
            if (target && target->GetVictim())
            {
                distance -= 10;
            }

            if (sServerFacade.isMoving(target) && target->GetVictim())
            {
                distance -= 10;
            }

            if (bot->InBattleGround())
            {
                distance += 20;
            }


            return (target && sServerFacade.getDistance2d(bot, target) < distance);
        }
    };

    class PowershiftTrigger : public Trigger
    {
    public:
        PowershiftTrigger(PlayerbotAI* ai) : Trigger(ai, "powershift") {}

        bool IsActive() override
        {
            if (ai->HasAura("cat form", bot) &&
                AI_VALUE2(uint8, "energy", "self target") < 20 &&
                AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.lowMana)
            {
                return true;
            }

            return false;
        }
    };

    class FerociousBiteTrigger : public ComboPointsAvailableTrigger
    {
    public:
        FerociousBiteTrigger(PlayerbotAI* ai) : ComboPointsAvailableTrigger(ai, 5) {}
    };

    // mod-playerbots parity (DRU-5): Ferocious Bite execute window — a
    // dying target dies to bite before Rip ticks out. Port of donor
    // FerociousBiteExecuteTrigger minus the WotLK clauses (no savage roar
    // in 1.18.1; the raw <20k-HP clause scaled to a plain HP% gate —
    // vanilla health pools make an absolute gate meaningless). Defined in
    // DruidTriggers.cpp.
    class FerociousBiteExecuteTrigger : public Trigger
    {
    public:
        FerociousBiteExecuteTrigger(PlayerbotAI* ai) : Trigger(ai, "ferocious bite execute") {}
        bool IsActive() override;
    };

    // mod-playerbots parity (DRU-5): Ferocious Bite timing window — at 5
    // combo points bite only when Rip is absent or healthy (>10 s left),
    // so bite never clips a Rip refresh. Port of donor
    // FerociousBiteTimeTrigger minus the savage-roar clause (no such spell
    // in 1.18.1). Defined in DruidTriggers.cpp.
    class FerociousBiteTimeTrigger : public Trigger
    {
    public:
        FerociousBiteTimeTrigger(PlayerbotAI* ai) : Trigger(ai, "ferocious bite time") {}
        bool IsActive() override;
    };

    BOOST_TRIGGER(BerserkTrigger, "berserk");

    class RebirthTrigger : public SpellTargetTrigger
    {
    public:
        RebirthTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "rebirth", "revive targets") {}
        std::string GetTargetName() override { return "party member to resurrect"; }

        bool IsTargetValid(Unit* target) override
        {
            return SpellTargetTrigger::IsTargetValid(target) && target->IsDead();
        }
    };

    // mod-playerbots parity (DRU-1): out-of-combat resurrection. Vanilla
    // druids have no normal resurrect, only Rebirth, so a dead party member
    // out of combat sits until a priest/paladin/shaman wakes up — or the
    // druid burns its 30 min battle rez. This trigger stays quiet while any
    // living groupmate of a resurrecting class (priest/paladin/shaman) is
    // around; their normal rez is always preferred. Gates on IsTargetValid
    // (public) rather than IsActive (private in SpellTargetTrigger), so the
    // base cooldown/spellbook checks and manual revive-target assignment
    // keep working untouched. Defined in DruidTriggers.cpp.
    class OocRebirthTrigger : public RebirthTrigger
    {
    public:
        OocRebirthTrigger(PlayerbotAI* ai) : RebirthTrigger(ai) {}
        std::string getName() override { return "ooc rebirth"; }
        bool IsTargetValid(Unit* target) override;
    };

    class InnervateTrigger : public SpellTargetTrigger
    {
    public:
        InnervateTrigger(PlayerbotAI* ai) : SpellTargetTrigger(ai, "innervate", "boost targets", true, true) {}
        std::string GetTargetName() override { return "self target"; }

        bool IsTargetValid(Unit* target) override
        {
            if (SpellTargetTrigger::IsTargetValid(target))
            {
                return ai->GetManaPercent(*target) < sPlayerbotAIConfig.lowMana;
            }

            return false;
        }
    };


    class ClearcastingTrigger : public HasAuraTrigger
    {
    public:
        ClearcastingTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "clearcasting") {}
    };
}
