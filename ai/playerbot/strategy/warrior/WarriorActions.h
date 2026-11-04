#pragma once
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/AoeFearPolicy.h"
#include "playerbot/RetaliationPolicy.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "playerbot/strategy/values/CcMarkHoldPolicy.h"
#include "playerbot/WarriorWwStancePolicy.h"
#include "playerbot/policy/combat/RaidSnarePolicy.h"

namespace ai
{
    bool PatchwerkThreatTank(PlayerbotAI* ai, Unit* target);

    // stances
    BUFF_ACTION_U(CastBattleStanceAction, "battle stance", !ai->IsTank(bot) && CastBuffSpellAction::isUseful());
    // Battle Stance only for the Mocking Blow second taunt: the plain battle
    // stance row refuses tanks, so this prerequisite carries its own name and
    // fires only while the mocking blow taunt trigger is active.
    class CastBattleStanceForMockingBlowAction : public CastBuffSpellAction
    {
    public:
        CastBattleStanceForMockingBlowAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "battle stance") {}
        bool isUseful() override
        {
            return CastBuffSpellAction::isUseful() && AI_VALUE2(bool, "trigger active", "mocking blow taunt");
        }

    protected:
        virtual std::string getName() override { return "battle stance for mocking blow"; }
    };
    BUFF_ACTION(CastDefensiveStanceAction, "defensive stance");
    BUFF_ACTION_U(CastBerserkerStanceAction, "berserker stance", !ai->IsTank(bot) && CastBuffSpellAction::isUseful());
    // Berserker Stance only for a Whirlwind that can actually fire: the
    // engine runs the prerequisite before isPossible, so a plain stance
    // node swaps (truncating rage) even when Whirlwind is on cooldown or
    // unaffordable. This prerequisite carries its own name and only dances
    // when the spell is off cooldown and the bar holds its cost.
    class CastBerserkerStanceForWhirlwindAction : public CastBuffSpellAction
    {
    public:
        CastBerserkerStanceForWhirlwindAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "berserker stance") {}
        bool isUseful() override
        {
            if (!CastBuffSpellAction::isUseful())
                return false;
            uint32 whirlwind = AI_VALUE2(uint32, "spell id", "whirlwind");
            unsigned tmRank = TacticalMasteryRank(bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_1),
                bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_2), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_3),
                bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_4), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_5));
            Group* group = bot->GetGroup();
            return ShouldWhirlwindStanceDance(whirlwind && sServerFacade.IsSpellReady(bot, whirlwind),
                ai->HasAura("berserker stance", bot),
                AI_VALUE2(uint8, "rage", "self target"), tmRank,
                group && group->isRaidGroup(),
                AI_VALUE2(bool, "trigger active", "melee light aoe"));
        }

    protected:
        virtual std::string getName() override { return "berserker stance for whirlwind"; }
    };
    // Berserker Stance only for a Pummel that can actually fire: same dance
    // trap as Whirlwind, plus the interrupt needs a live cast to land on.
    class CastBerserkerStanceForPummelAction : public CastBuffSpellAction
    {
    public:
        CastBerserkerStanceForPummelAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "berserker stance") {}
        bool isUseful() override
        {
            if (!CastBuffSpellAction::isUseful())
                return false;
            uint32 pummel = AI_VALUE2(uint32, "spell id", "pummel");
            Unit* target = AI_VALUE(Unit*, "interrupt target::pummel");
            unsigned tmRank = TacticalMasteryRank(bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_1),
                bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_2), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_3),
                bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_4), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_5));
            return ShouldPummelStanceDance(pummel && sServerFacade.IsSpellReady(bot, pummel),
                ai->HasAura("berserker stance", bot),
                AI_VALUE2(uint8, "rage", "self target"), tmRank,
                target && ai->IsInterruptableSpellCasting(target, "pummel", true));
        }

    protected:
        virtual std::string getName() override { return "berserker stance for pummel"; }
    };

    // shouts
    MELEE_ACTION_U(CastBattleShoutTauntAction, "battle shout", CastSpellAction::isUseful()); // useful to rebuff
    MELEE_DEBUFF_ACTION_R(CastDemoralizingShoutAction, "demoralizing shout", 8.0f); // low range debuff
    MELEE_ACTION(CastChallengingShoutAction, "challenging shout");
    class CastIntimidatingShoutAction : public CastMeleeDebuffSpellAction
    {
    public:
        CastIntimidatingShoutAction(PlayerbotAI* ai) : CastMeleeDebuffSpellAction(ai, "intimidating shout")
        {
            range = 8.0f;
        }
        bool isUseful() override
        {
            // Issue #383: the shout fears the pack and the feared mobs pull
            // neighbouring packs, so it never fires inside a dungeon or raid.
            // The shared rule lives in ai/playerbot/AoeFearPolicy.h; a
            // masterless pool bot alone in the open world keeps the old
            // critical-health emergency.
            Map* map = bot->GetMap();
            bool inInstance = map && (map->IsDungeon() || map->IsRaid());
            if (!ai::AoeFearAllowed(inInstance, ai->HasActivePlayerMaster(), ai->HasStrategy("aoe fear", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastMeleeDebuffSpellAction::isUseful();
        }
    };

    // arms
    MELEE_ACTION(CastHeroicStrikeAction, "heroic strike");
    REACH_ACTION(CastChargeAction, "charge", 8.0f);
    // Rend (10 rage) re-applied on every new target starved the 30-rage main
    // strike: Mortal Strike fired 0-3 times a run while Rend fired 41. Dps
    // warriors hold the strike's bar (ShouldRendForDps); tanks keep the old
    // unconditional row.
    class CastRendAction : public CastMeleeDebuffSpellAction
    {
    public:
        CastRendAction(PlayerbotAI* ai) : CastMeleeDebuffSpellAction(ai, "rend") {}

        bool isUseful() override
        {
            return CastMeleeDebuffSpellAction::isUseful() &&
                (ai->IsTank(bot) || ShouldRendForDps(ai->HasSpell("mortal strike") || ai->HasSpell("bloodthirst"),
                    AI_VALUE2(uint8, "rage", "self target")));
        }
    };
    MELEE_DEBUFF_ENEMY_ACTION(CastRendOnAttackerAction, "rend");
    // Thunder Clap is a rage trap in the Arms swing cycle (Mortal
    // Strike/Overpower/Sweeping Strikes out-damage it), so grouped PvE Arms
    // DPS leaves it to the tank. Fury keeps the row, and so do solo and
    // PvP/BG Arms: a solo bot has no tank covering the debuff, and in PvP
    // the snare matters more than the swing cycle.
    class CastThunderClapAction : public CastMeleeDebuffSpellAction
    {
    public:
        CastThunderClapAction(PlayerbotAI* ai) : CastMeleeDebuffSpellAction(ai, "thunder clap")
        {
            range = 8.0f;
        }
        bool isUseful() override
        {
            Group* group = bot->GetGroup();
            if (ShouldSkipThunderClapForArms(ai->IsTank(bot),
                ai->HasStrategy("arms", BotState::BOT_STATE_COMBAT),
                group && !group->isBGGroup(),
                bot->InBattleGround() || ai->HasStrategy("pvp", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastMeleeDebuffSpellAction::isUseful();
        }
    };
    class CastThunderClapSnareAction : public CastSnareSpellAction
    {
    public:
        CastThunderClapSnareAction(PlayerbotAI* ai) : CastSnareSpellAction(ai, "thunder clap") {}
        bool isUseful() override
        {
            Group* group = bot->GetGroup();
            if (ShouldSkipThunderClapForArms(ai->IsTank(bot),
                ai->HasStrategy("arms", BotState::BOT_STATE_COMBAT),
                group && !group->isBGGroup(),
                bot->InBattleGround() || ai->HasStrategy("pvp", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastSnareSpellAction::isUseful();
        }
    };
    SNARE_ACTION(CastHamstringAction, "hamstring");
    MELEE_ACTION(CastOverpowerAction, "overpower");
    MELEE_ACTION(CastMockingBlowAction, "mocking blow");
    class CastRetaliationAction : public CastBuffSpellAction
    {
    public:
        CastRetaliationAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "retaliation") {}

        bool isUseful() override
        {
            // 30 min cooldown, and it counters melee swings only: spend it when
            // two or more melee attackers are on the warrior (mod-playerbots
            // CastRetaliationAction), not on one mob or a caster.
            if (!CastBuffSpellAction::isUseful())
                return false;

            std::vector<RetaliationAttacker> attackers;
            for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
            {
                Unit* attacker = ai->GetUnit(guid);
                if (!attacker)
                    continue;

                attackers.push_back({attacker->IsAlive(), attacker->GetVictim() == bot,
                                     bot->GetDistance(attacker, SizeFactor::CombatReach) <= ATTACK_DISTANCE});
            }
            return RetaliationWorthCooldown(attackers);
        }
    };

    // Sweeping Strikes copies the warrior's swings onto a mob in melee reach for
    // its whole duration: it is not started, and a running one is cancelled,
    // beside a sheeped mob or a CC mark whose CC bot will (re)hold it (the
    // Blade Flurry rule, CcCancelSwingSplashBuff). The warrior's own victim is
    // not counted, as for Blade Flurry: the proc picks another mob.
    inline bool SweepingStrikesNearCc(PlayerbotAI* ai, Player* bot, float radius)
    {
        CcGuard ccGuard(ai);
        bool const protectedNear = ccGuard.ProtectedEnemyNear(bot, radius, true, bot->GetVictim());
        return CcCancelSwingSplashBuff(true, protectedNear, !protectedNear && ccGuard.ReadyCcMarkNear(bot, radius));
    }

    class CastSweepingStrikesAction : public CastBuffSpellAction
    {
    public:
        CastSweepingStrikesAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "sweeping strikes") {}
        bool isUseful() override
        {
            if (!CastBuffSpellAction::isUseful())
                return false;
            if (ai->IsTank(bot))
                return false;
            if (!AI_VALUE2(bool, "trigger active", "melee medium aoe"))
                return false;
            if (!ai->HasAura("battle stance", bot))
                return false;
            return !SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius);
        }
    };

    // arms talents
    MELEE_ACTION(CastMortalStrikeAction, "mortal strike");

    class CastMasterStrikeAction : public CastMeleeSpellAction
    {
    public:
        CastMasterStrikeAction(PlayerbotAI* ai) : CastMeleeSpellAction(ai, "master strike") {}

        bool isPossible() override
        {
            // Tortoise 54023 dispatches to a weapon-matched helper in core
            // (spell_warrior_master_strike). The spell template already
            // requires a weapon, but a missing main hand would otherwise burn
            // the 30s cooldown as a silent no-op.
            if (!CastMeleeSpellAction::isPossible())
                return false;
            return bot->GetWeaponForAttack(BASE_ATTACK, true, true) != nullptr;
        }
    };
    // fury
    MELEE_ACTION(CastCleaveAction, "cleave");
    MELEE_ACTION(CastExecuteAction, "execute");
    REACH_ACTION(CastInterceptAction, "intercept", 8.0f);
    // Intercept on another target is a gap closer too: the same creature aura
    // keep-out veto as CastReachTargetSpellAction (AiPlayerbot.AvoidCreatureAuras).
    class CastInterceptOnEnemyHealerAction : public CastSpellOnEnemyHealerAction
    {
    public:
        CastInterceptOnEnemyHealerAction(PlayerbotAI* ai) : CastSpellOnEnemyHealerAction(ai, "intercept") {}
        bool isUseful() override
        {
            return CastSpellOnEnemyHealerAction::isUseful() &&
                !(sPlayerbotAIConfig.avoidCreatureAuras && PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), GetTarget()));
        }
    };

    class CastInterceptOnSnareTargetAction : public CastSnareSpellAction
    {
    public:
        CastInterceptOnSnareTargetAction(PlayerbotAI* ai) : CastSnareSpellAction(ai, "intercept") {}
        bool isUseful() override
        {
            return CastSnareSpellAction::isUseful() &&
                !(sPlayerbotAIConfig.avoidCreatureAuras && PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), GetTarget()));
        }
    };
    MELEE_ACTION(CastSlamAction, "slam");
    BUFF_ACTION(CastBerserkerRageAction, "berserker rage");
    class CastWhirlwindAction : public CastMeleeSpellAction
    {
    public:
        CastWhirlwindAction(PlayerbotAI* ai) : CastMeleeSpellAction(ai, "whirlwind") {}
        bool isUseful() override
        {
            if (!CastMeleeSpellAction::isUseful())
                return false;
            if (ai->IsTank(bot))
                return true;
            return ai->HasAura("berserker stance", bot);
        }
    };
    MELEE_INTERRUPT_ACTION(CastPummelAction, "pummel");
    ENEMY_HEALER_ACTION(CastPummelOnEnemyHealerAction, "pummel");
    BUFF_ACTION(CastRecklessnessAction, "recklessness");

    // fury talents
    BUFF_ACTION(CastDeathWishAction, "death wish");
    MELEE_ACTION(CastBloodthirstAction, "bloodthirst");
    // Piercing Howl is a 10-yd AoE around the warrior, but its row is armed
    // by the "hamstring" snare unit, which the value returns anywhere in
    // spell range (30 yd): a runner or a healer-chaser out there armed the
    // row while the cast went to the melee target, already controlled and
    // never inside the Howl. Only fire on a unit the AoE can reach; in a
    // raid only a fleeing runner beside us (PiercingHowlAllowed).
    //
    // A refused Howl is useless in a raid (HowlGate::Skip): the engine
    // pushes a node's alternatives (Hamstring in both dps specs) only after
    // an impossible or failed action, never after a useless one.
    // In a dungeon the refusal stays impossible (HowlGate::FallBack) so the
    // Hamstring fallback runs. A warrior without the talent fails the base
    // isPossible on the same path.
    class CastPiercingHowlAction : public CastMeleeDebuffSpellAction
    {
    public:
        CastPiercingHowlAction(PlayerbotAI* ai) : CastMeleeDebuffSpellAction(ai, "piercing howl")
        {
            range = 8.0f;
        }

        bool isUseful() override
        {
            return CastMeleeDebuffSpellAction::isUseful() && Gate() != HowlGate::Skip;
        }

        bool isPossible() override
        {
            return CastMeleeDebuffSpellAction::isPossible() && Gate() == HowlGate::Cast;
        }

    private:
        HowlGate Gate()
        {
            Unit* snare = AI_VALUE2(Unit*, "snare target", "hamstring");
            Group* group = bot->GetGroup();
            bool fleeing = snare && (snare->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLEEING_MOTION_TYPE ||
                snare->GetMotionMaster()->GetCurrentMovementGeneratorType() == TIMED_FLEEING_MOTION_TYPE);
            return PiercingHowlGate(snare != nullptr, snare ? sServerFacade.GetDistance2d(bot, snare) : 0.0f,
                                    group && group->isRaidGroup(), fleeing);
        }
    };

    // protection
    MELEE_ACTION_U(CastTauntAction, "taunt", GetTarget() && GetTarget()->GetVictim() && GetTarget()->GetVictim() != bot);
    SNARE_ACTION(CastTauntOnSnareTargetAction, "taunt");

    // Taunt the mob picked by "attacker of non-tank target"; the cast reads
    // its real range from the spell and the reach prerequisite walks the
    // tank in.
    class CastTauntOnAttackerAction : public CastSnareSpellAction
    {
    public:
        CastTauntOnAttackerAction(PlayerbotAI* ai) : CastSnareSpellAction(ai, "taunt") {}

    protected:
        virtual std::string GetTargetName() override { return "attacker of non-tank target"; }
        virtual std::string getName() override { return "taunt on attacker"; }
    };
    BUFF_ACTION(CastBloodrageAction, "bloodrage");
    MELEE_INTERRUPT_ACTION(CastShieldBashAction, "shield bash");
    ENEMY_HEALER_ACTION(CastShieldBashOnEnemyHealerAction, "shield bash");
    MELEE_ACTION(CastRevengeAction, "revenge");
    BUFF_ACTION(CastShieldBlockAction, "shield block");
    MELEE_DEBUFF_ACTION_U(CastDisarmAction, "disarm", GetTarget() && GetTarget()->IsPlayer() ? !ai->IsRanged((Player*)GetTarget()) : CastMeleeDebuffSpellAction::isUseful());
    MELEE_DEBUFF_ENEMY_ACTION(CastDisarmOnAttackerAction, "disarm");
    BUFF_ACTION(CastShieldWallAction, "shield wall");
    // protection utility
    PROTECT_ACTION(CastInterveneAction, "intervene");

    // protection talents
    BUFF_ACTION(CastLastStandAction, "last stand");
    MELEE_ACTION(CastShieldSlamAction, "shield slam");
    MELEE_ACTION(CastConcussionBlowAction, "concussion blow");

    class CastBattleShoutAction : public CastBuffSpellAction
    {
    public:
        CastBattleShoutAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "battle shout") {}

        bool isUseful() override
        {
            static const std::vector<uint32> battleShoutIds = {6673, 5242, 6192, 11549, 11550, 11551, 25289};

            for (uint32 id : battleShoutIds)
            {
                if (bot->HasAura(id))
                    return false;
            }

            return CastSpellAction::isUseful();
        }
    };

    class CastSunderArmorAction : public CastMeleeDebuffSpellAction
    {
    public:
        CastSunderArmorAction(PlayerbotAI* ai) : CastMeleeDebuffSpellAction(ai, "sunder armor")
        {
            range = ATTACK_DISTANCE;
        }

        virtual bool isUseful() override
        {
            Unit* target = GetTarget();
            if (!target)
                return false;

            if (PatchwerkThreatTank(ai, target))
                return !ai->CanCastSpell("shield slam", target) && !ai->CanCastSpell("revenge", target);

            const bool isTank = ai->IsTank(bot);

            if (isTank)
            {
                uint32 bloodThirst = AI_VALUE2(uint32, "spell id", "bloodthirst");
                uint32 mortalStrike = AI_VALUE2(uint32, "spell id", "mortal strike");
                uint32 shieldSlam = AI_VALUE2(uint32, "spell id", "shield slam");

                // Defer sunder to a spender only when that spender can actually
                // fire: IsSpellReady is cooldown-only, so without the rage
                // gate below-20 rage vetoed sunder while the gated slam row
                // was inactive — a melee-only dead zone. Slam's row lives on
                // `light rage available`, so the veto follows the same
                // trigger; BT/MS rows are CanCast-gated (rage-aware already).
                bool slamLive = shieldSlam && sServerFacade.IsSpellReady(bot, shieldSlam) &&
                    AI_VALUE2(bool, "trigger active", "light rage available");
                // A live spender goes first, also while the stack is being built:
                // Shield Slam (1.18.1: 20 % AP, high threat) is the opener, Sunder the filler.
                if ((bloodThirst && sServerFacade.IsSpellReady(bot, bloodThirst)) ||
                    (mortalStrike && sServerFacade.IsSpellReady(bot, mortalStrike)) ||
                    slamLive)
                {
                    return false;
                }

                // Establish and refresh the stack before spending rage on other attacks.
                Aura* sunder = ai->GetAura("sunder armor", target);
                if (!target->IsPlayer() && (!sunder || sunder->GetStackAmount() < 5 ||
                    (sunder->GetAuraDuration() >= 0 && sunder->GetAuraDuration() <= 6000)))
                    return true;
            }

            // DPS warriors sunder only when no tank warrior is in the group
            // (donor CastSunderArmorAction::isUseful): with a real tank the
            // stack is their job and DPS rage is better spent on damage.
            if (!isTank)
            {
                Group* group = bot->GetGroup();
                if (group)
                {
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (!member || member == bot || !member->IsAlive() || !member->IsInWorld() ||
                            member->GetMapId() != bot->GetMapId())
                            continue;

                        if (member->GetClass() == CLASS_WARRIOR && ai->IsTank(member, false))
                            return false;
                    }
                }
            }
            if (isTank && !target->IsPlayer())
                return true;

            // Stack to 5, then only refresh an expiring stack (donor
            // CastSunderArmorAction::isUseful: stack < 5 or <=6s left).
            Aura* aura = ai->GetAura("sunder armor", target, false);
            return !aura || aura->GetStackAmount() < 5 || aura->GetAuraDuration() <= 6000;
        }
    };

    class UpdateWarriorPveStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarriorPveStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pve strats")
        {
            std::vector<std::string> strategiesRequired = { "arms" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "arms pve", strategiesRequired);

            strategiesRequired = { "arms", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe arms pve", strategiesRequired);

            strategiesRequired = { "arms", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff arms pve", strategiesRequired);

            strategiesRequired = { "arms", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost arms pve", strategiesRequired);

            strategiesRequired = { "arms", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc arms pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc arms pve", strategiesRequired);

            strategiesRequired = { "fury" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "fury pve", strategiesRequired);

            strategiesRequired = { "fury", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe fury pve", strategiesRequired);

            strategiesRequired = { "fury", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff fury pve", strategiesRequired);

            strategiesRequired = { "fury", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost fury pve", strategiesRequired);

            strategiesRequired = { "fury", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc fury pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc fury pve", strategiesRequired);

            strategiesRequired = { "protection/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "protection pve", strategiesRequired);

            strategiesRequired = { "protection/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe protection pve", strategiesRequired);

            strategiesRequired = { "protection/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff protection pve", strategiesRequired);

            strategiesRequired = { "protection/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost protection pve", strategiesRequired);

            strategiesRequired = { "protection/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc protection pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc protection pve", strategiesRequired);
        }
    };

    class UpdateWarriorPvpStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarriorPvpStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pvp strats")
        {
            std::vector<std::string> strategiesRequired = { "arms" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "arms pvp", strategiesRequired);

            strategiesRequired = { "arms", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe arms pvp", strategiesRequired);

            strategiesRequired = { "arms", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff arms pvp", strategiesRequired);

            strategiesRequired = { "arms", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost arms pvp", strategiesRequired);

            strategiesRequired = { "arms", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc arms pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc arms pvp", strategiesRequired);

            strategiesRequired = { "fury" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "fury pvp", strategiesRequired);

            strategiesRequired = { "fury", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe fury pvp", strategiesRequired);

            strategiesRequired = { "fury", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff fury pvp", strategiesRequired);

            strategiesRequired = { "fury", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost fury pvp", strategiesRequired);

            strategiesRequired = { "fury", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc fury pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc fury pvp", strategiesRequired);

            strategiesRequired = { "protection/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "protection pvp", strategiesRequired);

            strategiesRequired = { "protection/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe protection pvp", strategiesRequired);

            strategiesRequired = { "protection/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff protection pvp", strategiesRequired);

            strategiesRequired = { "protection/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost protection pvp", strategiesRequired);

            strategiesRequired = { "protection/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc protection pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc protection pvp", strategiesRequired);
        }
    };

    class UpdateWarriorRaidStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarriorRaidStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update raid strats")
        {
            std::vector<std::string> strategiesRequired = { "arms" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "arms raid", strategiesRequired);

            strategiesRequired = { "arms", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe arms raid", strategiesRequired);

            strategiesRequired = { "arms", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff arms raid", strategiesRequired);

            strategiesRequired = { "arms", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost arms raid", strategiesRequired);

            strategiesRequired = { "arms", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc arms raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc arms raid", strategiesRequired);

            strategiesRequired = { "fury" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "fury raid", strategiesRequired);

            strategiesRequired = { "fury", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe fury raid", strategiesRequired);

            strategiesRequired = { "fury", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff fury raid", strategiesRequired);

            strategiesRequired = { "fury", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost fury raid", strategiesRequired);

            strategiesRequired = { "fury", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc fury raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc fury raid", strategiesRequired);

            strategiesRequired = { "protection/tank" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "protection raid", strategiesRequired);

            strategiesRequired = { "protection/tank", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe protection raid", strategiesRequired);

            strategiesRequired = { "protection/tank", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff protection raid", strategiesRequired);

            strategiesRequired = { "protection/tank", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost protection raid", strategiesRequired);

            strategiesRequired = { "protection/tank", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc protection raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc protection raid", strategiesRequired);
        }
    };
}
