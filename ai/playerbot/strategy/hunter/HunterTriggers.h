#pragma once

#include "playerbot/HunterSwitchPolicy.h"
#include "playerbot/HunterAoePolicy.h"
#include "playerbot/HunterKitePolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/triggers/GenericTriggers.h"
#include "playerbot/strategy/hunter/HunterActions.h"
#include "playerbot/strategy/hunter/HunterValues.h"

namespace ai
{
    HAS_AURA_TRIGGER_TIME(FeignDeathTrigger, "feign death", 2);

    BEGIN_TRIGGER(HunterNoStingsActiveTrigger, Trigger)
    END_TRIGGER()

    class AspectOfTheHawkTrigger : public BuffTrigger
    {
    public:
        AspectOfTheHawkTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the hawk") {}
        // Opt-in (AiPlayerbot.HunterViperOnLowMana): stands down while the Viper is up and mana has not recovered.
        virtual bool IsActive() override;
    };

    // Opt-in (AiPlayerbot.HunterViperOnLowMana): low mana and no Viper yet.
    class AspectOfTheViperLowManaTrigger : public BuffTrigger
    {
    public:
        AspectOfTheViperLowManaTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "aspect of the viper") {}
        virtual bool IsActive() override;
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

    // A saved `.bot choice` order keeps its own aspect up: fires while an
    // aspect is ordered and missing. The upkeep rows cast exactly it.
    class ChoiceAspectTrigger : public Trigger
    {
    public:
        ChoiceAspectTrigger(PlayerbotAI* ai) : Trigger(ai, "choice aspect") {}
        bool IsActive() override;
        std::string GetTargetName() override { return "self target"; }
    };

    BEGIN_TRIGGER(HuntersPetDeadTrigger, Trigger)
    END_TRIGGER()

    BEGIN_TRIGGER(SafeToRevivePetTrigger, Trigger)
    END_TRIGGER()

    BEGIN_TRIGGER(HuntersPetLowHealthTrigger, Trigger)
    END_TRIGGER()

    BEGIN_TRIGGER(PetHasAggroTrigger, Trigger)
    END_TRIGGER()

    BEGIN_TRIGGER(HuntersPetMediumHealthTrigger, Trigger)
    END_TRIGGER()

    class HuntersMarkTrigger : public DebuffTrigger
    {
    public:
        HuntersMarkTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "hunter's mark") {}
        // Tank-target gate; see HunterTriggers.cpp.
        virtual bool IsActive() override;
    };

    class FreezingTrapTrigger : public HasCcTargetTrigger
    {
    public:
        FreezingTrapTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "freezing trap") {}
        // Free pick on the chooser's own target; see HunterTriggers.cpp.
        virtual bool IsActive() override;
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

    class VolleyChannelCheckTrigger : public Trigger
    {
    public:
        VolleyChannelCheckTrigger(PlayerbotAI* ai) : Trigger(ai, "volley channel check") {}
        bool IsActive() override;
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
    // Inside the shot's own minimum range (the dead zone), at the same
    // boundary EnemyTooCloseForAutoShotTrigger and the shot use.
    inline bool HunterTargetInDeadZone(PlayerbotAI* ai, Unit* target)
    {
        float maxRange = 0.0f;
        float minRange = 0.0f;
        if (!target || !ai->GetSpellRange("auto shot", &maxRange, &minRange) || minRange <= 0.0f)
            return false;
        return ai->GetBot()->GetDistance(target, SizeFactor::CombatReach) < minRange;
    }

    // The dead-zone step-out verdict (HunterSwitchPolicy.h) for the current
    // target; shared by the step-out row and the step-out action.
    inline HunterDeadZoneStep HunterDeadZoneStepFor(PlayerbotAI* ai, Unit* target)
    {
        HunterKitState* state = GetHunterKitState(ai);
        if (!state || !target)
            return HunterDeadZoneStep::None;
        Player* bot = ai->GetBot();
        bool const moving = !bot->IsStopped() || sServerFacade.isMoving(bot);
        return DecideHunterDeadZoneStep(*state, target->GetObjectGuid().GetRawValue(),
            WorldTimer::getMSTime(), HunterTargetInDeadZone(ai, target), target->GetVictim() == bot, moving);
    }

    class SwitchToRangedTrigger : public Trigger
    {
    public:
        SwitchToRangedTrigger(PlayerbotAI* ai) : Trigger(ai, "switch to ranged", 1) {}

        bool IsActive() override
        {
            // No level gate: the donor's SwitchToRangedTrigger has none either, and
            // below level 10 this is what hands the ranged kit back after a melee
            // trade - once the target is off the bot (pet or tank holds it, at
            // any distance) or immobilized (HunterSwitchPolicy.h).
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
            // Kit dwell (HunterSwitchPolicy.h). There is no failed-step-out
            // hold any more: the ranged kit never trades into melee for a mob
            // off the hunter (#1018), so there is nothing to flip back into.
            HunterKitState* state = GetHunterKitState(ai);
            bool const dwellElapsed = !state || HunterKitDwellElapsed(*state, WorldTimer::getMSTime());
            return ai::ShouldSwitchToRanged(true, targetOffBot, immobilized, tooSlowToFollow, distance, true,
                false, dwellElapsed);
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

            // Mob on the hunter only (the kite flee). A mob the pet, a tank
            // or anyone else holds gets the one-shot step-out below instead:
            // the generic flee re-fired every tick there without moving.
            if (target->GetVictim() != bot)
                return false;

            return bot->GetDistance(target, SizeFactor::CombatReach) < minRange;
        }
    };

    // Dead-zone step-out for a mob that is NOT on the hunter (pet, tank or
    // anyone else holds it): one step straight out of the shot's minimum
    // range per mob and episode (mod-playerbots 79bd428 steps out with
    // MoveAway / disengage the same way), held while the step is under way.
    // If the step does not get the hunter out it is retried farther (up to
    // kHunterStepOutMaxAttempts per episode); the hunter never trades into
    // melee for this mob (#1018).
    class HunterDeadZoneStepOutTrigger : public Trigger
    {
    public:
        HunterDeadZoneStepOutTrigger(PlayerbotAI* ai) : Trigger(ai, "hunter dead zone step out", 1) {}

        bool IsActive() override
        {
            if (!ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT) || !HunterHasLoadedRangedWeapon(ai))
                return false;

            HunterDeadZoneStep const step = HunterDeadZoneStepFor(ai, AI_VALUE(Unit*, "current target"));
            return step == HunterDeadZoneStep::StepOut || step == HunterDeadZoneStep::Hold;
        }
    };

    // Living group tank (the mark gate and the disengage gate share it).
    inline bool HunterHasLivingTank(PlayerbotAI* ai)
    {
        Group* group = ai->GetBot()->GetGroup();
        if (!group)
            return false;
        for (Player* member : LiveGroupMembers(group))
            if (member && member->IsAlive() && PlayerbotAI::IsTank(member))
                return true;
        return false;
    }

    // The kite's "usable ranged kit": ranged strategy with a loaded ranged
    // weapon. Shared by the kite trigger, the melee-chain veto and the
    // melee-switch veto.
    inline bool HunterKiteReady(PlayerbotAI* ai)
    {
        return ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT) && HunterHasLoadedRangedWeapon(ai);
    }

    inline bool HunterShouldKite(PlayerbotAI* ai, uint8_t attackerCount)
    {
        Unit* nearest = ai->GetAiObjectContext()->GetValue<Unit*>("closest attacker targeting me")->Get();
        float const nearestYd = nearest ? ai->GetBot()->GetCombatDistance(nearest) : 9999.0f;
        return ShouldHunterKite(attackerCount, HunterKiteReady(ai), nearestYd);
    }

    // Wing Clip the nearest and step out instead of trading with the pack
    // (HunterKitePolicy.h). In combat comes from the base trigger.
    class HunterMultipleAttackersTrigger : public MyAttackerCountTrigger
    {
    public:
        HunterMultipleAttackersTrigger(PlayerbotAI* ai) : MyAttackerCountTrigger(ai, kHunterKiteMinAttackers) {}

        bool IsActive() override
        {
            if (!MyAttackerCountTrigger::IsActive())
                return false;
            return HunterShouldKite(ai, AI_VALUE(uint8, "my attacker count"));
        }
    };

    // Disengage is a threat drop, not a move: it wipes the hunter off the
    // mob's threat list so a living tank can take the mob back. Only worth
    // the global when the mob fought is on the hunter (or climbing to it)
    // and stands in melee reach, and there is a tank to hand it to - solo
    // the mob just re-attacks the hunter. Readiness is rank-aware: "spell
    // id" resolves the highest known rank (0 when untrained) and each rank
    // has its own cooldown.
    class HunterDisengageTrigger : public Trigger
    {
    public:
        HunterDisengageTrigger(PlayerbotAI* ai) : Trigger(ai, "disengage on attacker", 1) {}

        bool IsActive() override
        {
            uint32 const rankId = AI_VALUE2(uint32, "spell id", "disengage");
            if (!rankId || !sServerFacade.IsSpellReady(bot, rankId))
                return false;

            if (!HunterHasLivingTank(ai))
                return false;

            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target || !bot->CanReachWithMeleeAutoAttack(target))
                return false;

            return AI_VALUE2(bool, "has aggro", "current target") ||
                AI_VALUE2(uint8, "threat", "current target") >= 80;
        }
    };

    // Raptor Strike / Wing Clip row: the generic 5 yd "enemy is close", but in
    // the ranged kit only for a mob that is on the hunter itself (the melee
    // chain and the kite snare). A mob the pet or a tank holds is left to the
    // dead-zone step-back, and the hunter goes back to shooting (mod-playerbots
    // 79bd428 GenericHunterStrategy ranks its step-back above wing clip). The
    // close kit (mob on the hunter, or no ammo) keeps the row as before.
    // While 2+ mobs are on the hunter the kite owns the fight: the chain
    // stands down and the hunter clips and steps instead.
    class HunterEnemyIsCloseTrigger : public EnemyInRangeTrigger
    {
    public:
        HunterEnemyIsCloseTrigger(PlayerbotAI* ai) : EnemyInRangeTrigger(ai, "hunter enemy is close", sPlayerbotAIConfig.tooCloseDistance) {}

        bool IsActive() override
        {
            if (!EnemyInRangeTrigger::IsActive())
                return false;

            if (!ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT))
                return true;

            // 2+ mobs on the hunter: the kite owns the fight, the melee
            // chain stands down.
            if (HunterShouldKite(ai, AI_VALUE(uint8, "my attacker count")))
                return false;

            Unit* target = AI_VALUE(Unit*, "current target");
            return target && target->GetVictim() == bot;
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
            // 2+ mobs on the hunter: no trading into melee, the kite owns
            // the fight. No ammo falls through above - nothing to kite with.
            if (HunterShouldKite(ai, AI_VALUE(uint8, "my attacker count")))
                return false;
            bool const fastOrFinisher = (target->GetSpeed(MOVE_RUN) > (bot->GetSpeed(MOVE_RUN) / 2)) || ((!bot->GetPet() || bot->GetPet()->IsDead()) && target->GetHealthPercent() < 50.f && target->IsCreature() && target->GetHealth() < bot->GetHealth());
            // Same true distance as the ranged half (see above): one call for
            // both switches and the telemetry row.
            float const distance = bot->GetDistance(target);
            // Only a mob on the hunter trades, after the kit dwell; a failed
            // dead-zone step-out for a mob someone else holds is retried
            // farther, not answered with melee (HunterSwitchPolicy.h, #1018).
            HunterKitState* state = GetHunterKitState(ai);
            bool const dwellElapsed = !state || HunterKitDwellElapsed(*state, WorldTimer::getMSTime());
            return ai::ShouldSwitchToMelee(true, target->GetVictim() == bot,
                IsImmobilizedStateCompat(target), fastOrFinisher, distance, true, false, dwellElapsed);
        }
    };

    // Multi-Shot flies only when the splash around the kill target hits a
    // second fighting enemy (HunterAoePolicy.h): the bare can-cast row fired
    // the shot on cooldown, so its share never scaled with the pack. The
    // count and the CC veto live in HunterTriggers.cpp.
    class HunterMultiShotTrigger : public SpellCanBeCastedTrigger
    {
    public:
        HunterMultiShotTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "multi-shot") {}
        virtual bool IsActive() override;
    };

    // BM cooldown: intimidation is a self-cast (BUFF_ACTION on self, core
    // rejects positive spells on hostile targets), ordered by the owner, so
    // the cast gate must run against the hunter itself — plus a live-pet
    // gate like KillCommandTrigger, since the stun lands via the pet.
    class IntimidationTrigger : public SpellCanBeCastedTrigger
    {
    public:
        IntimidationTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "intimidation") {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override
        {
            if (!SpellCanBeCastedTrigger::IsActive())
                return false;
            Unit* pet = AI_VALUE(Unit*, "pet target");
            return pet && pet->IsAlive();
        }
    };
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

        // AiPlayerbot.HunterCarveInReachOnly: no walk-in for Carve (HunterAoePolicy.h).
        bool IsActive() override
        {
            if (!SpellCanBeCastedTrigger::IsActive())
                return false;
            Unit* target = GetTarget();
            return target && ai::HunterCarveAllowed(sPlayerbotAIConfig.hunterCarveInReachOnly,
                bot->GetDistance(target, SizeFactor::CombatReach));
        }
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

    // [Turtle] Steady Shot filler (ranks 3035..45974: timed cast, no
    // cooldown, 1.5s GCD). A timed cast pauses the repeating Auto Shot, so it
    // only fires right after an Auto Shot, while the time to the next one
    // still covers the cast plus the core's resume windup
    // (HunterSteadyShotFitsAutoShotWindow). Otherwise the bot keeps
    // auto-shooting.
    class SteadyShotTrigger : public Trigger
    {
    public:
        SteadyShotTrigger(PlayerbotAI* ai) : Trigger(ai, "steady shot", 1) {}
        virtual std::string GetTargetName() override { return "current target"; }

        virtual bool IsActive() override
        {
            uint32 const spellId = AI_VALUE2(uint32, "spell id", "steady shot");
            if (!HunterSteadyShotFitsAutoShotWindow(ai, spellId) || !HunterHasRangedAmmo(ai))
                return false;

            Unit* target = GetTarget();
            return target && ai->CanCastSpell("steady shot", target, true, nullptr, true);
        }
    };

    // PET-5 (donor NoPetTrigger shape): purely petless + unmounted. The old
    // local addition gated this on Call Pet being unavailable, which starved
    // both call-pet nodes exactly when a dismissed pet waited to be called.
    // Ordering is by relevance now: instant Call Pet outranks the 20s Tame
    // wherever castable; Tame stays for the genuine no-call-pet case.
    class HunterNoPet : public Trigger
    {
    public:
        HunterNoPet(PlayerbotAI* ai) : Trigger(ai, "no pet", 1) {}
        virtual bool IsActive() override
        {
            if (AI_VALUE2(bool, "mounted", "self target"))
                return false;
            return !bot->GetPetGuid();
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
