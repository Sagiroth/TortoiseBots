#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    DEFLECT_TRIGGER(FireWardTrigger, "fire ward");
    DEFLECT_TRIGGER(FrostWardTrigger, "frost ward");

    class BlinkTrigger : public Trigger
    {
    public:
        BlinkTrigger(PlayerbotAI* ai) : Trigger(ai, "blink", 2) {}

        virtual bool IsActive() override
        {
            return bot->HasAuraType(SPELL_AURA_MOD_ROOT) ||
                   bot->HasAuraType(SPELL_AURA_MOD_STUN);
        }
    };

    // Kite escape (mod-playerbots GenericMageStrategy "enemy too close for
    // spell" -> blink back 35): blink is root/stun-only above, so a melee
    // mob walking up to the mage never triggers any escape. Fires when the
    // current target is attacking the mage and is within 8 yd, AND blink is
    // off cooldown (a target fighting someone else is no threat); frost nova
    // (EMERGENCY, cc strategy) still owns the rooted-pack case first.
    class BlinkBackTrigger : public Trigger
    {
    public:
        BlinkBackTrigger(PlayerbotAI* ai) : Trigger(ai, "blink back", 2) {}
        bool IsActive() override;
    };

    // A wand is running while Frostbolt or Fireball is held back only by the
    // GCD each wand shot renews on Turtle (WandPolicy.h): stop the wand
    // so the mage casts again instead of wanding at full mana.
    class WandBlocksNukeTrigger : public WandBlocksCastTrigger
    {
    public:
        WandBlocksNukeTrigger(PlayerbotAI* ai) : WandBlocksCastTrigger(ai, "wand blocks nuke") {}
        bool IsActive() override;
    };

    // Too little mana for the spec's filler nuke, above or below the 5%
    // line (NukeUnaffordable, WandPolicy.h).
    class NukeUnaffordableTrigger : public Trigger
    {
    public:
        NukeUnaffordableTrigger(PlayerbotAI* ai, std::string name, std::string nuke)
            : Trigger(ai, name), nuke(nuke) {}
        bool IsActive() override;

    private:
        std::string nuke;
    };

    class ArcaneIntellectOnPartyTrigger : public BuffOnPartyTrigger
    {
    public:
        ArcaneIntellectOnPartyTrigger(PlayerbotAI* ai) : BuffOnPartyTrigger(ai, "arcane intellect", 4) {}
        virtual bool IsActive() override { return BuffOnPartyTrigger::IsActive() && !ai->HasAura("arcane brilliance", GetTarget()); }
    };

    class ArcaneIntellectTrigger : public BuffTrigger
    {
    public:
        ArcaneIntellectTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "arcane intellect", 4) {}
        virtual bool IsActive() override { return BuffTrigger::IsActive() && !ai->HasAura("arcane brilliance", GetTarget()); }
    };

    class ArcaneBrillianceOnPartyTrigger : public GreaterBuffOnPartyTrigger
    {
    public:
        ArcaneBrillianceOnPartyTrigger(PlayerbotAI* ai) : GreaterBuffOnPartyTrigger(ai, "arcane brilliance", "arcane intellect", 4) {}
    };

    class AnyMageArmorTrigger : public BuffTrigger
    {
    public:
        AnyMageArmorTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "any mage armor", 5) {}
        virtual bool IsActive() override;
    };

    class MageArmorTrigger : public BuffTrigger
    {
    public:
        MageArmorTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "mage armor", 5) {}
        virtual bool IsActive() override;
    };

    class IceArmorTrigger : public BuffTrigger
    {
    public:
        IceArmorTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "ice armor", 5) {}
        virtual bool IsActive() override;
    };

    class FireballTrigger : public DebuffTrigger
    {
    public:
        FireballTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "fireball") {}
	};

    class PyroblastTrigger : public DebuffTrigger
    {
    public:
        PyroblastTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "pyroblast", 10) {}
    };

    class CounterspellInterruptSpellTrigger : public InterruptSpellTrigger
    {
    public:
        CounterspellInterruptSpellTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "counterspell") {}
    };

    class CombustionTrigger : public BoostTrigger
    {
    public:
        CombustionTrigger(PlayerbotAI* ai) : BoostTrigger(ai, "combustion") {}
    };

    class PolymorphTrigger : public HasCcTargetTrigger
    {
    public:
        PolymorphTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "polymorph") {}
    };

    class RemoveCurseTrigger : public NeedCureTrigger
    {
    public:
        RemoveCurseTrigger(PlayerbotAI* ai) : NeedCureTrigger(ai, "remove curse", DISPEL_CURSE) {}
    };

    class PartyMemberRemoveCurseTrigger : public PartyMemberNeedCureTrigger
    {
    public:
        PartyMemberRemoveCurseTrigger(PlayerbotAI* ai) : PartyMemberNeedCureTrigger(ai, "remove curse", DISPEL_CURSE) {}
    };

    class CounterspellEnemyHealerTrigger : public InterruptEnemyHealerTrigger
    {
    public:
        CounterspellEnemyHealerTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "counterspell") {}
    };

    class ArcanePowerTrigger : public BuffTrigger
    {
    public:
        ArcanePowerTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "arcane power") {}
        virtual bool IsActive() override;
    };

    class PresenceOfMindTrigger : public BuffTrigger
    {
    public:
        PresenceOfMindTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "presence of mind") {}
    };

    // MAG-5: rupture self buff (52502/52588) absent — cast rupture so the
    // missiles that follow hit +19%. See ArcaneRupturePolicy.h.
    class ArcaneRuptureTrigger : public Trigger
    {
    public:
        ArcaneRuptureTrigger(PlayerbotAI* ai) : Trigger(ai, "arcane rupture") {}
        bool IsActive() override;
    };

    HAS_AURA_TRIGGER(PresenceOfMindAuraTrigger, "presence of mind");

    // Turtle fire talent Hot Streak (51927/51928): crits stack a cast-time
    // reduction aura for the next Pyroblast (proc 51930/51931). The talent
    // auras share the "Hot Streak" name, so this MUST NOT be a name-based
    // HasAuraTrigger — see HotStreakTrigger::IsActive in MageTriggers.cpp.
    class HotStreakTrigger : public Trigger
    {
    public:
        HotStreakTrigger(PlayerbotAI* ai) : Trigger(ai, "hot streak") {}
        bool IsActive() override;
    };

    class ManaShieldTrigger : public BuffTrigger
    {
    public:
        ManaShieldTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "mana shield", 5) {}
        virtual bool IsActive() override;
    };

    class IceBarrierTrigger : public BuffTrigger
    {
    public:
        IceBarrierTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "ice barrier", 5) {}
    };

    // Frostbolt/fireball had no combat trigger at all: the specs sat at
    // ACTION_IDLE and fell through to the wand fallback (Issue #221).
    CAN_CAST_TRIGGER(FrostboltTrigger, "frostbolt");
    CAN_CAST_TRIGGER(FireBlastTrigger, "fire blast");

    class IciclesTrigger : public SpellCanBeCastedTrigger
    {
    public:
        IciclesTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "icicles") {}
        bool IsActive() override;
    };

    class IciclesChannelCheckTrigger : public Trigger
    {
    public:
        IciclesChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "icicles channel check") {}
        bool IsActive() override;
    };

    class EvocationChannelCheckTrigger : public Trigger
    {
    public:
        EvocationChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "evocation channel check") {}
        bool IsActive() override;
    };

    // MAG-2: own flamestrike cast started <=6s ago and the pack is still
    // grouped — channel blizzard on the burning ground (window counts from
    // cast start, not impact). See FlamestrikeWindowPolicy.h
    // (the donor's dynobj check cannot port: no aura→dynobj link here).
    class FlamestrikeWindowTrigger : public Trigger
    {
    public:
        FlamestrikeWindowTrigger(PlayerbotAI* ai) : Trigger(ai, "flamestrike window") {}
        bool IsActive() override;
    };

    // A pack clustered inside Arcane Explosion's 10yd radius that the splash
    // can safely burst (density + CC interlock, holder safety).
    class ArcaneExplosionWindowTrigger : public AoeTrigger
    {
    public:
        ArcaneExplosionWindowTrigger(PlayerbotAI* ai) : AoeTrigger(ai, 3, 10.0f) {}
        bool IsActive() override;
    };

    // A mob attacking the mage within Frost Nova's radius: the tank's held
    // pack must not burn the emergency cooldown.
    class FrostNovaWindowTrigger : public Trigger
    {
    public:
        FrostNovaWindowTrigger(PlayerbotAI* ai) : Trigger(ai, "frost nova window") {}
        bool IsActive() override;
    };

    // Frost's flamestrike row: same pack test as "ranged medium aoe", plus
    // the 10 s opener gate blizzard already has (CastBlizzardAction), so a
    // frost mage does not open a pack with a fire nuke before the tank holds
    // it.
    class RangedMediumAoeAfterOpenerTrigger : public RangedMediumAoeTrigger
    {
    public:
        RangedMediumAoeAfterOpenerTrigger(PlayerbotAI* ai) : RangedMediumAoeTrigger(ai) {}
        std::string getName() override { return "ranged medium aoe after opener"; }
        bool IsActive() override
        {
            return ai->GetCombatStartTime() && (time(0) - ai->GetCombatStartTime()) > 10 &&
                   RangedMediumAoeTrigger::IsActive();
        }
    };

    class BlizzardChannelCheckTrigger : public Trigger
    {
    public:
        BlizzardChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "blizzard channel check") {}
        bool IsActive() override;
    };

    class FireSpellsLocked : public Trigger
    {
    public:
        FireSpellsLocked(PlayerbotAI* ai) : Trigger(ai, "fire spells locked", 2) {}

        virtual bool IsActive() override
        {
            return !sServerFacade.IsSpellReady(bot, 133)      //fireball
                && !sServerFacade.IsSpellReady(bot, 2948);    //scorch
        }
    };

    // Turtle names the talent ranks (11095/12872/12873/12874/12875) and the target
    // debuff (22959) "Fire Vulnerability"; no spell is called "Improved
    // Scorch", so a name-based DebuffTrigger never matched. Id-based instead.
    class NoImprovedScorchDebuffTrigger : public Trigger
    {
    public:
        NoImprovedScorchDebuffTrigger(PlayerbotAI* ai) : Trigger(ai, "no improved scorch") {}
        virtual bool IsActive() override;
        virtual std::string GetTargetName() override { return "current target"; }
    };

    class NoFireVulnerabilityTrigger : public Trigger
    {
    public:
        NoFireVulnerabilityTrigger(PlayerbotAI* ai) : Trigger(ai, "no fire vulnerability") {}

        virtual bool IsActive() override
        {
            // Required non-player target
            Unit* target = GetTarget();
            if (target && !target->IsPlayer())
            {
                // Required Improved Scorch talent
                if (bot->HasSpell(11095) || bot->HasSpell(12872) || bot->HasSpell(12873) || bot->HasSpell(12874) || bot->HasSpell(12875))
                {
                    // Check if have max stacks of fire vulnerability expiring in less than 7 seconds
                    Aura* aura = ai->GetAura(22959, target);
                    if (aura)
                    {
                        // Check if the stack is at the maximum
                        const uint32 maxStackAmount = aura->GetSpellProto()->StackAmount;
                        const uint32 stackAmount = aura->GetHolder()->GetStackAmount();
                        if (stackAmount >= maxStackAmount)
                        {
                            // Check if the aura is about to expire
                            if (aura->GetHolder()->GetAuraDuration() > 7000)
                            {
                                return false;
                            }
                        }
                    }

                    return true;
                }
            }

            return false;
        }

        virtual std::string GetTargetName() override { return "current target"; }
    };

    class ColdSnapTrigger : public Trigger
    {
    public:
        ColdSnapTrigger(PlayerbotAI* ai) : Trigger(ai, "cold snap", 2) {}

        virtual bool IsActive() override
        {
            const uint32 coldSnapId = AI_VALUE2(uint32, "spell id", "cold snap");
            if (!coldSnapId || !sServerFacade.IsSpellReady(bot, coldSnapId))
                return false;

            // Cold Snap is useful only when it can actually reset a Frost
            // cooldown. SpellIdValue selects the highest rank the bot knows,
            // so optional/talent spells remain naturally unavailable.
            static const char* frostCooldowns[] = {
                "frost nova", "cone of cold", "ice barrier", "ice block", "frost ward"
            };
            for (const char* spell : frostCooldowns)
            {
                const uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
                if (spellId && !sServerFacade.IsSpellReady(bot, spellId))
                    return true;
            }

            return false;
        }
    };

    class NoFoodTrigger : public Trigger
    {
    public:
        NoFoodTrigger(PlayerbotAI* ai) : Trigger(ai, "no food trigger", 5) {}

        virtual bool IsActive() override
        {
            return !ai->HasCheat(BotCheatMask::item) && AI_VALUE2(std::list<Item*>, "inventory items", "conjured food").empty();
        }
    };

    class NoDrinkTrigger : public Trigger
    {
    public:
        NoDrinkTrigger(PlayerbotAI* ai) : Trigger(ai, "no drink trigger", 5) {}

        virtual bool IsActive() override
        {
            return !ai->HasCheat(BotCheatMask::item) && AI_VALUE2(std::list<Item*>, "inventory items", "conjured water").empty();
        }
    };

    class NoManaGemTrigger : public Trigger
    {
    public:
        NoManaGemTrigger(PlayerbotAI* ai) : Trigger(ai, "no mana gem trigger", 5) {}

        virtual bool IsActive() override
        {
            if (!ai->HasCheat(BotCheatMask::item))
            {
                const std::vector<uint32> manaGemIds = { 5514, 5513, 8007, 8008 };
                for (const uint32 manaGemId : manaGemIds)
                {
                    if (bot->HasItemCount(manaGemId, 1))
                    {
                        return false;
                    }
                }

                return true;
            }

            return false;
        }
    };
}
