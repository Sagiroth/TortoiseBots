#pragma once

#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/ChangeStrategyAction.h"
#include "playerbot/policy/class/ShamanFireGatePolicy.h"
#include "playerbot/policy/class/ShamanTankPolicy.h"

namespace ai
{
    BUFF_ACTION(CastGhostWolfAction, "ghost wolf");
    // Self-target direct heals share the party downrank swap: the base
    // HealPartyMemberAction::Execute only runs for the "on party" lines,
    // so the self rows ("healing wave", "lesser healing wave") always paid
    // top rank (live raid 2026-10-14: Healing Wave top rank 100x). Inherit
    // the party action for DownrankSpellId + Execute, override the target
    // back to self and the name back to the plain spell (the party base
    // reports "<spell> on party").
    class CastSelfDownrankHealingAction : public HealPartyMemberAction
    {
    public:
        CastSelfDownrankHealingAction(PlayerbotAI* ai, std::string spell, uint8 estAmount, HealManaEfficiency efficiency)
            : HealPartyMemberAction(ai, spell, estAmount, efficiency), spell(spell) {}
        std::string getName() override { return spell; }
        std::string GetTargetName() override { return "self target"; }
    private:
        std::string spell;
    };

    class CastLesserHealingWaveAction : public CastSelfDownrankHealingAction
    {
    public:
        CastLesserHealingWaveAction(PlayerbotAI* ai) : CastSelfDownrankHealingAction(ai, "lesser healing wave", 25, HealManaEfficiency::LOW) {}
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }
        bool isUseful() override
        {
            if (bot->IsInCombat() && (ai->IsTank(bot) || ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastSelfDownrankHealingAction::isUseful();
        }
    };

    class CastLesserHealingWaveOnPartyAction : public HealPartyMemberAction
    {
    public:
        CastLesserHealingWaveOnPartyAction(PlayerbotAI* ai) : HealPartyMemberAction(ai, "lesser healing wave", 25, HealManaEfficiency::LOW) {}
    };

    class CastHealingWaveAction : public CastSelfDownrankHealingAction
    {
    public:
        CastHealingWaveAction(PlayerbotAI* ai) : CastSelfDownrankHealingAction(ai, "healing wave", 50, HealManaEfficiency::MEDIUM) {}
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }
        bool isUseful() override
        {
            if (bot->IsInCombat() && (ai->IsTank(bot) || ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastSelfDownrankHealingAction::isUseful();
        }
    };

    class CastHealingWaveOnPartyAction : public HealPartyMemberAction
    {
    public:
        CastHealingWaveOnPartyAction(PlayerbotAI* ai) : HealPartyMemberAction(ai, "healing wave", 50, HealManaEfficiency::MEDIUM) {}
    };

    class CastChainHealAction : public CastAoeHealSpellAction
    {
    public:
        CastChainHealAction(PlayerbotAI* ai) : CastAoeHealSpellAction(ai, "chain heal") {}
    };

    // Tortoise keeps the classic-era Earth Shield and Water Shield
    // variants as native player spells.
    class CastEarthShieldAction : public CastBuffSpellAction
    {
    public:
        CastEarthShieldAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "earth shield") {}
    };

    class CastWaterShieldAction : public CastBuffSpellAction
    {
    public:
        CastWaterShieldAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "water shield") {}
    };

	class CastLightningShieldAction : public CastBuffSpellAction
    {
	public:
		CastLightningShieldAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "lightning shield") {}
	};

    class CastRockbiterWeaponAction : public CastEnchantItemAction
    {
    public:
        CastRockbiterWeaponAction(PlayerbotAI* ai) : CastEnchantItemAction(ai, "rockbiter weapon") {}
    };

    class CastFlametongueWeaponAction : public CastEnchantItemAction
    {
    public:
        CastFlametongueWeaponAction(PlayerbotAI* ai) : CastEnchantItemAction(ai, "flametongue weapon") {}
    };

    class CastFrostbrandWeaponAction : public CastEnchantItemAction
    {
    public:
        CastFrostbrandWeaponAction(PlayerbotAI* ai) : CastEnchantItemAction(ai, "frostbrand weapon") {}
    };

    class CastWindfuryWeaponAction : public CastEnchantItemAction
    {
    public:
        CastWindfuryWeaponAction(PlayerbotAI* ai) : CastEnchantItemAction(ai, "windfury weapon") {}
    };

    // Weapon-imbue upkeep (issue #406 follow-up): the strategies used to queue
    // one fixed imbue per spec while ShamanWeaponTrigger stayed true on any
    // known imbue, so every shaman below 30 failed the windfury cast every
    // tick (the chain still landed rockbiter but the failed head logged an
    // ACTION_LOOP). This action picks the best imbue the bot actually knows
    // via HasSpell (ranked in ShamanImbuePolicy.h: enhancement windfury >
    // flametongue > frostbrand > rockbiter, others flametongue first) and
    // runs only that one through DoSpecificAction - the inner cast never
    // reports IMPOSSIBLE for untrained imbues, so nothing is recorded.
    // Weapon-imbue upkeep is a no-op unless the weapon lacks an imbue and the
    // bot can cast right now (live ACTION_LOOP: sitting/casting/stunned bots
    // failed it every tick). False both when nothing is known yet (fresh
    // level 1) and when there is simply nothing to do.
    class CastShamanWeaponUpkeepAction : public Action
    {
    public:
        CastShamanWeaponUpkeepAction(PlayerbotAI* ai) : Action(ai, "shaman weapon upkeep") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class CastTotemAction : public CastBuffSpellAction
    {
    public:
        CastTotemAction(PlayerbotAI* ai, std::string spell) : CastBuffSpellAction(ai, spell) {}

        bool Execute(Event& event) override
        {
            bool ok = CastBuffSpellAction::Execute(event);
            if (ok)
            {
                time_t now = time(0);
                SET_AI_VALUE2(time_t, "manual time", "last totem cast time", now);
                if (IsAirTotem(name))
                    SET_AI_VALUE2(time_t, "manual time", "last air totem cast time", now);
            }
            return ok;
        }

        bool isUseful() override
        {
            time_t lastCast = AI_VALUE2(time_t, "manual time", "last totem cast time");
            if (lastCast && time(0) - lastCast < 4)
                return false;

            if (sServerFacade.isMoving(bot))
            {
                // No totems on the run: they would be left behind and burn
                // mana for zero benefit. Only an engaged melee fight keeps
                // the totem useful while moving.
                Unit* target = AI_VALUE(Unit*, "current target");
                if (!bot->IsInCombat() || !target || bot->GetDistance(target) > 10.0f)
                    return false;
            }
            if (!CastBuffSpellAction::isUseful())
                return false;

            if (AI_VALUE2(bool, "has totem", name))
                return false;

            if (IsAirTotem(name))
            {
                time_t lastAir = AI_VALUE2(time_t, "manual time", "last air totem cast time");
                if (lastAir && time(0) - lastAir < 4)
                    return false;
                if (HasAnyAirTotem())
                    return false;
            }

            Group* group = bot->GetGroup();
            if (!group)
                return true;

            for (Player* member : LiveGroupMembers(group))
            {
                if (!member || !sServerFacade.IsAlive(member))
                    continue;

                if (member == bot)
                    continue;

                if (!member->IsInWorld() || member->GetMapId() != bot->GetMapId())
                    continue;

                if (!bot->IsWithinDistInMap(member, 100.0f, false))
                    continue;

                // Totem buffs land under effect names ("Windfury Totem
                // Effect"), never under the cast spell's name — and the
                // name scan only matches exact spell names, so it always
                // reads "missing" and every row stays USELESS-looping
                // while a same-group totem already covers the member
                // (live Naxx 2026-10-14: 3x air + 2x water rows USELESS
                // every tick all fight). Match by the totem spell's aura
                // id instead: any rank's aura on the member counts.
                if (!HasTotemAura(GetSpellName(), member))
                    return true;
            }

            return false;
        }

    protected:
        std::string GetTargetName() override { return "self target"; }
        std::string GetReachActionName() override { return "reach party member for totem"; }

        NextAction** getPrerequisites() override
        {
            if (!bot->GetGroup())
                return CastBuffSpellAction::getPrerequisites();

            // A tank shaman in combat must never leave its target to place a totem;
            // drop totems where it stands without walking away from the mob.
            if (bot->IsInCombat() && (ai->IsTank(bot) || ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT)))
                return CastBuffSpellAction::getPrerequisites();

            const std::string qualifiersStr = Qualified::MultiQualify({ GetSpellName(), "party member for totem" }, "::");
            return NextAction::merge(
                NextAction::array(0, new NextAction("reach party member for totem::" + qualifiersStr), nullptr),
                CastBuffSpellAction::getPrerequisites());
        }

        static bool IsAirTotem(std::string const& totemName)
        {
            return totemName == "windfury totem" ||
                   totemName == "tranquil air totem" ||
                   totemName == "grace of air totem" ||
                   totemName == "grounding totem" ||
                   totemName == "nature resistance totem" ||
                   totemName == "windwall totem" ||
                   totemName == "sentry totem";
        }

        bool HasAnyAirTotem()
        {
            return AI_VALUE2(bool, "has totem", "windfury totem") ||
                   AI_VALUE2(bool, "has totem", "tranquil air totem") ||
                   AI_VALUE2(bool, "has totem", "grace of air totem") ||
                   AI_VALUE2(bool, "has totem", "grounding totem") ||
                   AI_VALUE2(bool, "has totem", "nature resistance totem") ||
                   AI_VALUE2(bool, "has totem", "windwall totem") ||
                   AI_VALUE2(bool, "has totem", "sentry totem");
        }

        // True when the member carries any rank's aura of this totem
        // spell (own or a group mate's — either one covers them).
        bool HasTotemAura(std::string const& totemSpell, Player* member)
        {
            if (!member)
                return false;
            std::uint32_t spellId = AI_VALUE2(std::uint32_t, "spell id", totemSpell);
            if (!spellId)
                return false;
            SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
            if (!info)
                return false;
            for (std::uint32_t effect = 0; effect < (std::uint32_t)MAX_EFFECT_INDEX; ++effect)
            {
                std::uint32_t auraId = info->EffectTriggerSpell[effect];
                if (!auraId)
                    continue;
                if (ai->HasAura(auraId, member))
                    return true;
            }
            return ai->HasAura(spellId, member);
        }
    };

    class CastStoneskinTotemAction : public CastTotemAction
    {
    public:
        CastStoneskinTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "stoneskin totem") {}
    };

    class CastStoneclawTotemAction : public CastTotemAction
    {
    public:
        CastStoneclawTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "stoneclaw totem") {}
    };

    class CastGroundingTotemAction : public CastTotemAction
    {
    public:
        CastGroundingTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "grounding totem") {}
    };

    class CastEarthbindTotemAction : public CastTotemAction
    {
    public:
        CastEarthbindTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "earthbind totem") {}
    };

    class CastTremorTotemAction : public CastTotemAction
    {
    public:
        CastTremorTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "tremor totem") {}
    };

    class CastStrengthOfEarthTotemAction : public CastTotemAction
    {
    public:
        CastStrengthOfEarthTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "strength of earth totem") {}
    };

    class CastManaSpringTotemAction : public CastTotemAction
    {
    public:
        CastManaSpringTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "mana spring totem") {}
        virtual bool isUseful() override { return CastTotemAction::isUseful() && !AI_VALUE2(bool, "has totem", "healing stream totem"); }
    };

    // Mana Tide shares the water slot with Mana Spring / Healing Stream,
    // but it is a 5-minute cooldown raid battery, not an upkeep totem: it
    // must land while Spring/Stream is up (live H7+H8: 0 casts while
    // healers sat at 18-30% mana — the water aura scan read "covered"
    // and the cast never started). Skip the cover scan; the trigger rows
    // ("low mana", "healer low mana") plus the engine cooldown check own
    // the timing. Other water totems keep the scan.
    class CastManaTideTotemAction : public CastTotemAction
    {
    public:
        CastManaTideTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "mana tide totem") {}
        virtual std::string GetTargetName() override { return "self target"; }
        bool isUseful() override
        {
            if (sServerFacade.isMoving(bot))
            {
                Unit* target = AI_VALUE(Unit*, "current target");
                if (!bot->IsInCombat() || !target || bot->GetDistance(target) > 10.0f)
                    return false;
            }
            return CastBuffSpellAction::isUseful();
        }
    };

	class CastHealingStreamTotemAction : public CastTotemAction
	{
	public:
		CastHealingStreamTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "healing stream totem") {}
	};

    class CastFrostResistanceTotemAction : public CastTotemAction
    {
    public:
        CastFrostResistanceTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "frost resistance totem") {}
    };

    class CastFireResistanceTotemAction : public CastTotemAction
    {
    public:
        CastFireResistanceTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "fire resistance totem") {}
    };

    class CastNatureResistanceTotemAction : public CastTotemAction
    {
    public:
        CastNatureResistanceTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "nature resistance totem") {}
    };

    class CastDiseaseCleansingTotemAction : public CastTotemAction
    {
    public:
        CastDiseaseCleansingTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "disease cleansing totem") {}
    };

    class CastPoisonCleansingTotemAction : public CastTotemAction
    {
    public:
        CastPoisonCleansingTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "poison cleansing totem") {}
    };

    class CastCleansingTotemAction : public CastTotemAction
    {
    public:
        CastCleansingTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "cleansing totem") {}
    };

    class CastFlametongueTotemAction : public CastTotemAction
    {
    public:
        CastFlametongueTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "flametongue totem") {}
        virtual bool isUseful() override { return CastTotemAction::isUseful() && !AI_VALUE2(bool, "has totem", "magma totem"); }
    };

    class CastWindfuryTotemAction : public CastTotemAction
    {
    public:
        CastWindfuryTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "windfury totem") {}
    };

    class CastWindwallTotemAction : public CastTotemAction
    {
    public:
        CastWindwallTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "windwall totem") {}
    };

    class CastGraceOfAirTotemAction : public CastTotemAction
    {
    public:
        CastGraceOfAirTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "grace of air totem") {}
    };

    // Turtle custom (25908): threat-reduction air totem, preferred for
    // caster subgroups when trained. No strategy gate: the air trigger's
    // subgroup census picks it, the slot check quiets it once down.
    class CastTranquilAirTotemAction : public CastTotemAction
    {
    public:
        CastTranquilAirTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "tranquil air totem") {}
    };

    class CastSearingTotemAction : public CastTotemAction
    {
    public:
        CastSearingTotemAction(PlayerbotAI* ai) : CastTotemAction(ai, "searing totem") {}
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isUseful() override { return CastTotemAction::isUseful() && !AI_VALUE2(bool, "has totem", "flametongue totem"); }
    };

    class CastMagmaTotemAction : public CastMeleeSpellAction
    {
    public:
        CastMagmaTotemAction(PlayerbotAI* ai) : CastMeleeSpellAction(ai, "magma totem") {}
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isUseful() override { return CastMeleeSpellAction::isUseful() && !AI_VALUE2(bool, "has totem", name); }
    };

    class CastFireNovaAction : public CastSpellAction
    {
    public:
        // Casts the totem DROP. "Fire Nova" is the totem's own pulse (8349
        // line) and is never in the spellbook, so the old spell name never
        // resolved. The action keeps the name "fire nova" (AoE-off veto and
        // NextAction rows match it). The drop lands at our feet, so it
        // targets the bot as the magma and searing totems do: a self-only
        // spell at an enemy target fails the range check in isPossible.
        CastFireNovaAction(PlayerbotAI* ai) : CastSpellAction(ai, "fire nova totem") {}
        std::string getName() override { return "fire nova"; }
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isUseful() override
        {
            // 1.12 Fire Nova is a totem DROP (not a pulse of an existing
            // totem): refuse to place it when the target is out of
            // detonation range of the drop point, like the magma melee
            // gate. Gate lives in ShamanFireGatePolicy.h so the unit test
            // pins this exact logic.
            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target)
                return false;
            return FireNovaDropShouldFire(bot->GetDistance(target)) && CastSpellAction::isUseful();
        }
    };

	class CastAncestralSpiritAction : public ResurrectPartyMemberAction
	{
	public:
		CastAncestralSpiritAction(PlayerbotAI* ai) : ResurrectPartyMemberAction(ai, "ancestral spirit") {}
	};

	class CastPurgeAction : public CastSpellAction
	{
	public:
		CastPurgeAction(PlayerbotAI* ai) : CastSpellAction(ai, "purge") {}
	};

    // Tank taunt (Tortoise 51365, 5 yd, 10 s CD, off-GCD): the lose-aggro
    // row queues it at warrior protection priority (INTERRUPT+1) when a mob
    // peels onto a non-tank member; the target check mirrors the warrior
    // CastTauntAction guard (victim exists and is not us).
    MELEE_ACTION_U(CastEarthshakerSlamAction, "earthshaker slam", GetTarget() && GetTarget()->GetVictim() && GetTarget()->GetVictim() != bot);

    // Taunt the mob picked by "attacker of non-tank target". CastMeleeSpellAction's
    // own reach prerequisite targets that mob (a taunt past 5 yards fails, so
    // the shaman walks to the healer's attacker first); no strategy node may
    // override it with an unqualified "reach melee", which would target the
    // current target instead.
    class CastEarthshakerSlamOnAttackerAction : public CastEarthshakerSlamAction
    {
    public:
        CastEarthshakerSlamOnAttackerAction(PlayerbotAI* ai) : CastEarthshakerSlamAction(ai) {}

    protected:
        virtual std::string GetTargetName() override { return "attacker of non-tank target"; }
        virtual std::string getName() override { return "earthshaker slam on attacker"; }
    };

    // Tauren tank racial (Tortoise 45500, quest 40348, 90 s CD): damage plus
    // slow, NOT a taunt. Fires in melee when the strikes are cooling down
    // (the TotemicSlamTrigger ready-gate); off-GCD damage on every mob in
    // reach via the melee-aoe spell base, like war stomp.
    class CastTotemicSlamAction : public CastMeleeAoeSpellAction
    {
    public:
        CastTotemicSlamAction(PlayerbotAI* ai) : CastMeleeAoeSpellAction(ai, "totemic slam", 5.0f)
        {
            range = radius;
        }

        bool isUseful() override
        {
            return GetSpellID() && sServerFacade.IsSpellReady(bot, GetSpellID()) && CastMeleeAoeSpellAction::isUseful();
        }
    };

    class CastStormstrikeAction : public CastMeleeSpellAction
    {
	public:
		CastStormstrikeAction(PlayerbotAI* ai) : CastMeleeSpellAction(ai, "stormstrike") {}
	};
    BUFF_ACTION(CastElementalMasteryAction, "elemental mastery");

    class CastLightningStrikeAction : public CastMeleeSpellAction
    {
    public:
        // Tortoise 51387: weapon strike plus empowered active-shield release
        // (nature ranks 51386/52419/52421, shield script 52679). Core owns
        // charge consumption; the trigger requires an active shield.
        CastLightningStrikeAction(PlayerbotAI* ai) : CastMeleeSpellAction(ai, "lightning strike") {}
    };

    class CastEarthquakeAction : public CastSpellAction
    {
    public:
        // Tortoise 48306-8: primary damage plus splash-around-primary and a
        // 4s aftershock. Density/CC gating comes from the generic AoE
        // triggers; core excludes the primary from its own splash.
        CastEarthquakeAction(PlayerbotAI* ai) : CastSpellAction(ai, "earthquake") {}
    };

    class CastSpiritLinkOnPartyTankAction : public BuffOnTankAction
    {
    public:
        // Tortoise 51363: 10min damage-redistribution link on the tank.
        CastSpiritLinkOnPartyTankAction(PlayerbotAI* ai) : BuffOnTankAction(ai, "spirit link") {}
    };

    class CastAncestralSwiftnessAction : public CastBuffSpellAction
    {
    public:
        // Tortoise 16188: next Nature cast <10s becomes instant (damage
        // reduced); healing unaffected. Pair with Healing Wave via the
        // active-aura consumer trigger.
        CastAncestralSwiftnessAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "ancestral swiftness") {}
    };

    class CastBloodlustAction : public CastBuffSpellAction
    {
    public:
        CastBloodlustAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "bloodlust") {}
    };

    class CastWaterBreathingAction : public CastBuffSpellAction
    {
    public:
        CastWaterBreathingAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "water breathing") {}
    };

    class CastWaterWalkingAction : public CastBuffSpellAction
    {
    public:
        CastWaterWalkingAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "water walking") {}
    };

    class CastWaterBreathingOnPartyAction : public BuffOnPartyAction
    {
    public:
        CastWaterBreathingOnPartyAction(PlayerbotAI* ai) : BuffOnPartyAction(ai, "water breathing") {}
    };

    class CastWaterWalkingOnPartyAction : public BuffOnPartyAction
    {
    public:
        CastWaterWalkingOnPartyAction(PlayerbotAI* ai) : BuffOnPartyAction(ai, "water walking") {}
    };

    class CastFlameShockAction : public CastRangedDebuffSpellAction
    {
    public:
        CastFlameShockAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "flame shock") {}
    };

    class CastEarthShockAction : public CastRangedDebuffSpellAction
    {
    public:
        CastEarthShockAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "earth shock") {}
    };

    // Interrupt rows only: "earth shock" itself stays on the current target
    // (rotation and lose-aggro rows cast it there).
    class CastEarthShockInterruptAction : public CastInterruptSpellAction
    {
    public:
        CastEarthShockInterruptAction(PlayerbotAI* ai) : CastInterruptSpellAction(ai, "earth shock") {}
        // A real nuke: the threat back-off keeps gating it (AiPlayerbot.InterruptIgnoresThreat covers pure interrupts).
        bool IgnoresThreatCap() const override { return false; }
    };

    ENEMY_HEALER_ACTION(CastEarthShockOnEnemyHealerAction, "earth shock");

    class CastFrostShockAction : public CastSnareSpellAction
    {
    public:
        CastFrostShockAction(PlayerbotAI* ai) : CastSnareSpellAction(ai, "frost shock") {}
    };

    class CastChainLightningAction : public CastSpellAction
    {
    public:
        CastChainLightningAction(PlayerbotAI* ai) : CastSpellAction(ai, "chain lightning") {}
        // Hits every mob in the area, loose adds included: the party threat back-off
        // checks it against the "aoe" threat (mod-playerbots types it Aoe too).
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }

        virtual bool isUseful() override
        {
            if ((ai->IsTank(bot) || ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT)) &&
                !TankHardCastAllowed(AI_VALUE(uint8, "attackers count")))
                return false;

            return CastSpellAction::isUseful();
        }
    };

    class CastLightningBoltAction : public CastSpellAction
    {
    public:
        CastLightningBoltAction(PlayerbotAI* ai) : CastSpellAction(ai, "lightning bolt") {}

        virtual bool isUseful() override
        {
            if ((ai->IsTank(bot) || ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT)) &&
                !TankHardCastAllowed(AI_VALUE(uint8, "attackers count")))
                return false;

            return CastSpellAction::isUseful();
        }
    };

    class CastCurePoisonAction : public CastCureSpellAction
    {
    public:
        CastCurePoisonAction(PlayerbotAI* ai) : CastCureSpellAction(ai, "cure poison") {}
    };

    class CastCurePoisonOnPartyAction : public CurePartyMemberAction
    {
    public:
        CastCurePoisonOnPartyAction(PlayerbotAI* ai) : CurePartyMemberAction(ai, "cure poison", DISPEL_POISON) {}
    };

    class CastCureDiseaseAction : public CastCureSpellAction
    {
    public:
        CastCureDiseaseAction(PlayerbotAI* ai) : CastCureSpellAction(ai, "cure disease") {}
    };

    class CastCureDiseaseOnPartyAction : public CurePartyMemberAction
    {
    public:
        CastCureDiseaseOnPartyAction(PlayerbotAI* ai) : CurePartyMemberAction(ai, "cure disease", DISPEL_DISEASE) {}
        virtual std::string getName() override { return "cure disease on party"; }
    };


    class CastTotemicRecall : public CastBuffSpellAction
    {
    public:
        CastTotemicRecall(PlayerbotAI* ai) : CastBuffSpellAction(ai, "totemic recall") {}
    };

    class SetTotemBars : public Action
    {
    public:
        SetTotemBars(PlayerbotAI* ai) : Action(ai, "setup shaman totembar")
        {
        }
    };

    class UpdateShamanPveStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateShamanPveStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pve strats")
        {
            std::vector<std::string> strategiesRequired = {"elemental"};
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "elemental pve", strategiesRequired);

            strategiesRequired = {"elemental", "aoe"};
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe elemental pve", strategiesRequired);

            strategiesRequired = { "elemental", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc elemental pve", strategiesRequired);

            strategiesRequired = { "elemental", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure elemental pve", strategiesRequired);

            strategiesRequired = { "elemental", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems elemental pve", strategiesRequired);

            strategiesRequired = { "elemental", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff elemental pve", strategiesRequired);

            strategiesRequired = { "elemental", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost elemental pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost elemental pve", strategiesRequired);

            strategiesRequired = { "enhancement" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "enhancement pve", strategiesRequired);

            strategiesRequired = { "enhancement", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe enhancement pve", strategiesRequired);

            strategiesRequired = { "enhancement", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc enhancement pve", strategiesRequired);

            strategiesRequired = { "enhancement", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure enhancement pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc tank shaman pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure tank shaman pve", strategiesRequired);

            strategiesRequired = { "enhancement", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems enhancement pve", strategiesRequired);

            strategiesRequired = { "enhancement", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff enhancement pve", strategiesRequired);

            strategiesRequired = { "enhancement", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost enhancement pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost enhancement pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "tank shaman pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe tank shaman pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems tank shaman pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff tank shaman pve", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost tank shaman pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost tank shaman pve", strategiesRequired);

            strategiesRequired = { "restoration" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff restoration pve", strategiesRequired);

            strategiesRequired = { "restoration", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost restoration pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost restoration pve", strategiesRequired);

            strategiesRequired = { "offheal" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offheal pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offheal pve", strategiesRequired);

            strategiesRequired = { "offdps" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offdps pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offdps pve", strategiesRequired);
        }
    };

    class UpdateShamanPvpStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateShamanPvpStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pvp strats")
        {
            std::vector<std::string> strategiesRequired = { "elemental" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff elemental pvp", strategiesRequired);

            strategiesRequired = { "elemental", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost elemental pvp", strategiesRequired);

            strategiesRequired = { "enhancement" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "enhancement pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe enhancement pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc enhancement pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure enhancement pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc tank shaman pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure tank shaman pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems enhancement pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff enhancement pvp", strategiesRequired);

            strategiesRequired = { "enhancement", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost enhancement pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost enhancement pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "tank shaman pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe tank shaman pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems tank shaman pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff tank shaman pvp", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost tank shaman pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost tank shaman pvp", strategiesRequired);

            strategiesRequired = { "restoration" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "restoration pvp", strategiesRequired);

            strategiesRequired = { "restoration", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe restoration pvp", strategiesRequired);

            strategiesRequired = { "restoration", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc elemental pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc elemental pvp", strategiesRequired);

            strategiesRequired = { "restoration", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure restoration pvp", strategiesRequired);

            strategiesRequired = { "restoration", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems restoration pvp", strategiesRequired);

            strategiesRequired = { "restoration", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff restoration pvp", strategiesRequired);

            strategiesRequired = { "restoration", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost restoration pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost restoration pvp", strategiesRequired);

            strategiesRequired = { "offheal" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offheal pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offheal pvp", strategiesRequired);

            strategiesRequired = { "offdps" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offdps pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offdps pvp", strategiesRequired);
        }
    };

    class UpdateShamanRaidStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateShamanRaidStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update raid strats")
        {
            std::vector<std::string> strategiesRequired = { "elemental" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff elemental raid", strategiesRequired);

            strategiesRequired = { "elemental", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost elemental raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost elemental raid", strategiesRequired);

            strategiesRequired = { "enhancement" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "enhancement raid", strategiesRequired);

            strategiesRequired = { "enhancement", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe enhancement raid", strategiesRequired);

            strategiesRequired = { "enhancement", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc enhancement raid", strategiesRequired);

            strategiesRequired = { "enhancement", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure enhancement raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc tank shaman raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure tank shaman raid", strategiesRequired);

            strategiesRequired = { "enhancement", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems enhancement raid", strategiesRequired);

            strategiesRequired = { "enhancement", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff enhancement raid", strategiesRequired);

            strategiesRequired = { "enhancement", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost enhancement raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost enhancement raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "tank shaman raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe tank shaman raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems tank shaman raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff tank shaman raid", strategiesRequired);

            strategiesRequired = { "tank shaman/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost tank shaman raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost tank shaman raid", strategiesRequired);

            strategiesRequired = { "restoration" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "cure" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cure restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cure restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "totems" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "totems restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "totems restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff restoration raid", strategiesRequired);

            strategiesRequired = { "restoration", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost restoration raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost restoration raid", strategiesRequired);

            strategiesRequired = { "offheal" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offheal raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offheal raid", strategiesRequired);

            strategiesRequired = { "offdps" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "offdps raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "offdps raid", strategiesRequired);
        }
    };
}
