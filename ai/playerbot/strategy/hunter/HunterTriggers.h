#pragma once

#include "playerbot/HunterSwitchPolicy.h"
#include "playerbot/strategy/triggers/GenericTriggers.h"
#include "playerbot/strategy/hunter/HunterActions.h"

namespace ai
{
    HAS_AURA_TRIGGER_TIME(FeignDeathTrigger, "feign death", 2);

    BEGIN_TRIGGER(HunterNoStingsActiveTrigger, Trigger)
    END_TRIGGER()

    class AspectOfTheHawkTrigger : public BuffTrigger
    {
    public:
        AspectOfTheHawkTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the hawk") {}
    };

    class AspectOfTheWildTrigger : public BuffTrigger
    {
    public:
        AspectOfTheWildTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the wild") {}
    };

    class AspectOfThePackTrigger : public BuffTrigger
    {
    public:
        AspectOfThePackTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the pack") {}
    };

    class AspectOfTheMonkeyTrigger : public BuffTrigger
    {
    public:
        AspectOfTheMonkeyTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the monkey") {}
    };

    class AspectOfTheBeastTrigger : public BuffTrigger
    {
    public:
        AspectOfTheBeastTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the beast") {}
    };

    class AspectOfTheCheetahTrigger : public BuffTrigger
    {
    public:
        AspectOfTheCheetahTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the cheetah") {}
    };

    BEGIN_TRIGGER(HuntersPetDeadTrigger, Trigger)
    END_TRIGGER()

    BEGIN_TRIGGER(HuntersPetLowHealthTrigger, Trigger)
    END_TRIGGER()

    class HuntersMarkTrigger : public DebuffTrigger
    {
    public:
        HuntersMarkTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "hunter's mark") {}
    };

    class FreezingTrapTrigger : public HasCcTargetTrigger
    {
    public:
        FreezingTrapTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "freezing trap") {}

        bool IsActive() override
        {
            // Check if feign death not on cooldown
            if (sServerFacade.IsSpellReady(bot, 5384))
            {
                return HasCcTargetTrigger::IsActive();
            }

            return false;
        }
    };

    class FrostTrapTrigger : public MeleeLightAoeTrigger
    {
    public:
        FrostTrapTrigger(PlayerbotAI* ai, std::string spell = "frost trap") : MeleeLightAoeTrigger(ai)
        {
            spellId = AI_VALUE2(uint32, "spell id", spell);
        }

        bool IsActive() override
        {
            // Check if feign death not on cooldown
            if (!sServerFacade.IsSpellReady(bot, 5384))
            {
                return false;
            }

            return sServerFacade.IsSpellReady(bot, spellId) && MeleeLightAoeTrigger::IsActive();
        }

    private:
        uint32 spellId;
    };

    class ExplosiveTrapTrigger : public RangedMediumAoeTrigger
    {
    public:
        ExplosiveTrapTrigger(PlayerbotAI* ai, std::string spell = "explosive trap") : RangedMediumAoeTrigger(ai)
        {
            spellId = AI_VALUE2(uint32, "spell id", spell);
        }

        bool IsActive() override
        {
            // Check if feign death not on cooldown
            if (!sServerFacade.IsSpellReady(bot, 5384))
            {
                return false;
            }

            return sServerFacade.IsSpellReady(bot, spellId) && RangedMediumAoeTrigger::IsActive();
        }

    private:
        uint32 spellId;
    };

    class RapidFireTrigger : public BuffTrigger
    {
    public:
        RapidFireTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "rapid fire") {}
    };

    class TrueshotAuraTrigger : public BuffTrigger
    {
    public:
        TrueshotAuraTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "trueshot aura") {}
    };

    class SerpentStingOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        SerpentStingOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "serpent sting") {}
        virtual bool IsActive() override;
    };

    class ViperStingOnAttackerTrigger : public DebuffOnAttackerTrigger
    {
    public:
        ViperStingOnAttackerTrigger(PlayerbotAI* ai) : DebuffOnAttackerTrigger(ai, "viper sting") {}
        virtual bool IsActive() override;
    };

    BEGIN_TRIGGER(HunterPetNotHappy, Trigger)
    END_TRIGGER()

    class ConsussiveShotSnareTrigger : public SnareTargetTrigger
    {
    public:
        ConsussiveShotSnareTrigger(PlayerbotAI* ai) : SnareTargetTrigger(ai, "concussive shot") {}
    };

    SNARE_TRIGGER(ScatterShotSnareTrigger, "scatter shot");

    class ScareBeastTrigger : public HasCcTargetTrigger
    {
    public:
        ScareBeastTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "scare beast") {}
    };

    class HunterLowAmmoTrigger : public AmmoCountTrigger
    {
    public:
        HunterLowAmmoTrigger(PlayerbotAI* ai) : AmmoCountTrigger(ai, "ammo", 1, 30) {}
        virtual bool IsActive() override { return bot->GetGroup() && (AI_VALUE2(uint32, "item count", "ammo") < 100) && (AI_VALUE2(uint32, "item count", "ammo") > 0); }
    };

    class HunterNoAmmoTrigger : public AmmoCountTrigger
{
public:
    HunterNoAmmoTrigger(PlayerbotAI* ai)
        : AmmoCountTrigger(ai, "ammo", 1, 10), lastCheckTime(0)
    {
    }

    virtual bool IsActive() override
    {
        uint32 now = time(nullptr);

        // Only check every 3 seconds to avoid spamming
        if (now - lastCheckTime < 3)
            return false;

        lastCheckTime = now;

        uint32 ammoId = bot->GetUInt32Value(PLAYER_AMMO_ID);

        // No ammo equipped at all
        if (ammoId == 0)
            return AI_VALUE2(uint32, "item count", "ammo") > 0;

        // Check if we still have THIS ammo in inventory
        uint32 count = AI_VALUE2(uint32, "item count", std::to_string(ammoId));

        // If equipped ammo is gone, but we have other ammo → trigger
        return count == 0 && AI_VALUE2(uint32, "item count", "ammo") > 0;
    }

private:
    time_t lastCheckTime;
};
    class HunterHasAmmoTrigger : public AmmoCountTrigger
    {
    public:
        HunterHasAmmoTrigger(PlayerbotAI* ai) : AmmoCountTrigger(ai, "ammo", 1, 10) {}
        virtual bool IsActive() override { return !AmmoCountTrigger::IsActive(); }
    };

    // Dead-zone hysteresis (pool 1v1 fix, Oct 2026): the switch pair shares one
    // boundary so a mob pacing at exactly 8 yd cannot flip the kit every tick
    // (inter-switch p50 is 15 s, 37% land within 10 s). Melee at 5 yd and
    // below, ranged back at 10 yd and above: between the two the bot holds
    // whatever kit it has. Both match the donor's AND/OR shape (victim/distance
    // on the melee side, off-bot/immobilized/slow/far on the ranged side) -
    // only the distance half is pinned to the hysteresis edge. No level gate
    // on either, mirroring the donor.
    class SwitchToRangedTrigger : public Trigger
    {
    public:
        SwitchToRangedTrigger(PlayerbotAI* ai) : Trigger(ai, "switch to ranged", 1) {}

        bool IsActive() override
        {
            // No level gate: the donor's SwitchToRangedTrigger has none either, and
            // below level 10 this is what hands the ranged kit back after a melee
            // trade - once the target is off the bot, immobilized, too slow to
            // follow, or the bot has made distance (10+ yd, the hysteresis edge
            // in HunterSwitchPolicy.h).
            bool hasAmmo = ai->HasCheat(BotCheatMask::item) || AI_VALUE2(uint32, "item count", "ammo");
            if (!hasAmmo)
                return false;

            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target || !ai->HasStrategy("close", BotState::BOT_STATE_COMBAT))
                return false;
            // True edge-to-edge distance, not the stance-projected "distance"
            // value (a point 1.5+ yd in front of the mob): the projection let
            // SwitchToMelee fire at a logged 5-8 yd and hid which branch the
            // 5/10 yd band gates. Telemetry logs this same call.
            float const distance = bot->GetDistance(target);
            bool const targetOffBot = target->GetVictim() != bot;
            bool const immobilized = IsImmobilizedStateCompat(target);
            bool const tooSlowToFollow = target->GetSpeed(MOVE_RUN) <= (bot->GetSpeed(MOVE_RUN) / 2) && !((!bot->GetPet() || bot->GetPet()->IsDead()) && target->IsCreature() && target->GetHealthPercent() < 50.f && target->GetHealth() < bot->GetHealth());
            return ai::ShouldSwitchToRanged(true, targetOffBot, immobilized, tooSlowToFollow, distance, true);
        }
    };

    // The shot's own minimum range is the hunter's dead zone: inside it no
    // ranged attack is possible, and the generic "enemy too close for spell"
    // flee is suppressed while a fast target is glued to the bot (RangeTriggers.h
    // "can't add distance" guard) - the state that otherwise leaves an armed
    // low-level hunter standing. This trigger is that step back, at the exact
    // boundary the shot itself uses (CastSpellAction::isPossible).
    class EnemyTooCloseForAutoShotTrigger : public Trigger
    {
    public:
        EnemyTooCloseForAutoShotTrigger(PlayerbotAI* ai) : Trigger(ai, "enemy too close for auto shot", 1) {}

        bool IsActive() override
        {
            if (!ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT) || !HunterHasLoadedRangedWeapon(ai))
                return false;

            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target)
                return false;

            float maxRange = 0.0f;
            float minRange = 0.0f;
            if (!ai->GetSpellRange("auto shot", &maxRange, &minRange) || minRange <= 0.0f)
                return false;

            return bot->GetDistance(target, SizeFactor::CombatReach) < minRange;
        }
    };

    class SwitchToMeleeTrigger : public Trigger
    {
    public:
        SwitchToMeleeTrigger(PlayerbotAI* ai) : Trigger(ai, "switch to melee", 1) {}

        bool IsActive() override
        {
            // No level gate (the donor's switch has none): a glued mob inside the
            // shot's dead zone cannot be shot, and holding the ranged kit only
            // yields the zero-damage dead-zone step-back, so below level 10 the
            // hunter now trades into melee instead. SwitchToRangedTrigger gives the
            // ranged kit back once the target is off the bot or out of melee
            // (10+ yd, the hysteresis edge in HunterSwitchPolicy.h).
            bool hasAmmo = ai->HasCheat(BotCheatMask::item) || AI_VALUE2(uint32, "item count", "ammo");
            if (!hasAmmo)
                return true;

            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target || !ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT))
                return false;
            bool const fastOrFinisher = (target->GetSpeed(MOVE_RUN) > (bot->GetSpeed(MOVE_RUN) / 2)) || ((!bot->GetPet() || bot->GetPet()->IsDead()) && target->GetHealthPercent() < 50.f && target->IsCreature() && target->GetHealth() < bot->GetHealth());
            // Same true distance as the ranged half (see above): one call for
            // both switches and the telemetry row.
            float const distance = bot->GetDistance(target);
            return ai::ShouldSwitchToMelee(true, target->GetVictim() == bot,
                IsImmobilizedStateCompat(target), fastOrFinisher, distance, true);
        }
    };

    CAN_CAST_TRIGGER(MultishotCanCastTrigger, "multi-shot");
    SNARE_TRIGGER(IntimidationSnareTrigger, "intimidation");
    CAN_CAST_TRIGGER(CounterattackCanCastTrigger, "counterattack");
    SNARE_TRIGGER(WybernStingSnareTrigger, "wyvern sting");
    CAN_CAST_TRIGGER(MongooseBiteCastTrigger, "mongoose bite");
    BOOST_TRIGGER(BestialWrathBoostTrigger, "bestial wrath");

    class KillCommandTrigger : public SpellCanBeCastedTrigger
    {
    public:
        KillCommandTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "kill command") {}
        bool IsActive() override
        {
            // Tortoise 41827 has casterAuraState 6 (crit window): core
            // CanCastSpell enforces the window, so no DBC guessing here.
            // The damage is dealt by the pet (80% pet AP), hence the live-pet
            // gate; core resolves explicit -> selected -> pet-victim target.
            if (!SpellCanBeCastedTrigger::IsActive())
                return false;
            Unit* pet = AI_VALUE(Unit*, "pet target");
            return pet && pet->IsAlive();
        }
    };

    class CarveTrigger : public SpellCanBeCastedTrigger
    {
    public:
        CarveTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "carve") {}
    };


    class ViperStingTrigger : public DebuffTrigger
    {
    public:
        ViperStingTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "viper sting") {}

        virtual bool IsActive() override
        {
            return DebuffTrigger::IsActive() && AI_VALUE2(uint8, "mana", "current target") >= 10;
        }
    };

    // Aimed shot needs the 31-point MM talent (19434): BM/survival and
    // sub-40 MM hunters never fire it, and arcane shot was only its
    // fallback - so they fell back to auto shot forever (Issue #221).
    class ArcaneShotTrigger : public Trigger
    {
    public:
        ArcaneShotTrigger(PlayerbotAI* ai) : Trigger(ai, "arcane shot", 2) {}
        virtual std::string GetTargetName() override { return "current target"; }

        virtual bool IsActive() override
        {
            Unit* target = GetTarget();
            if (!target)
                return false;
            // Aimed shot owners keep their opener; everyone else rotates
            // arcane shot (level 8) whenever it is castable.
            if (bot->HasSpell(19434))
                return false;
            return ai->CanCastSpell("arcane shot", target, true, nullptr, true);
        }
    };

    class AimedShotTrigger : public Trigger
    {
    public:
        AimedShotTrigger(PlayerbotAI* ai) : Trigger(ai, "aimed shot", 2) {}
        virtual std::string GetTargetName() override { return "current target"; }

        virtual bool IsActive() override
        {
            if (!bot->HasSpell(19434) || !sServerFacade.IsSpellReady(bot, 19434))
                return false;

            Unit* target = GetTarget();
            if (!target)
                return false;

            float distanceTo = AI_VALUE2(float, "distance", GetTargetName());
            if (target->GetVictim() != bot && sServerFacade.IsDistanceGreaterOrEqualThan(distanceTo, 8.0f))
                return true;

            // victim
            if (target->GetVictim() == bot)
            {
                if (sServerFacade.IsDistanceGreaterOrEqualThan(distanceTo, 15.0f))
                    return true;
            }
            return false;
        }
    };

    class HunterNoPet : public Trigger
    {
    public:
        HunterNoPet(PlayerbotAI* ai) : Trigger(ai, "no pet", 1) {}
        virtual bool IsActive() override
        {
            if (AI_VALUE2(bool, "mounted", "self target"))
            return false;

            if (bot->GetPetGuid())
            return false;

            if (ai->CanCastSpell("call pet", bot, 0))
            return false;

            return ai->CanCastSpell("tame beast", bot, 0);
        }
    };

    class StealthedNearbyTrigger : public Trigger
    {
    public:
        StealthedNearbyTrigger(PlayerbotAI* ai) : Trigger(ai, "stealthed nearby", 5) {}
        virtual bool IsActive() override
        {
            if (!bot->HasSpell(1543))
                return false;

            Unit* target = AI_VALUE(Unit*, "nearest stealthed unit");
            return target;
        }
    };
}
