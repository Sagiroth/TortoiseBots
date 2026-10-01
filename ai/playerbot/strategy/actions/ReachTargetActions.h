#pragma once
#include "playerbot/GroupMembers.h"
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/strategy/values/MoveStyleValue.h"
#include "GenericSpellActions.h"
#include "playerbot/PlayerbotFactory.h"
#include "AI/CreatureAI.h"
#include <ctime>

namespace ai
{
    class ReachTargetAction : public MovementAction, public Qualified
    {
    public:
        ReachTargetAction(PlayerbotAI* ai, std::string name, float range = 0.0f) : MovementAction(ai, name), Qualified(), range(range), spellName("") {}

    private:
        // Give-up bookkeeping for a hostile target that is out of line of sight (see Execute).
        ObjectGuid noLosTarget;
        uint32 noLosSinceMs = 0;
        float noLosBestDist = 0.0f;
        float noLosStartX = 0.0f;
        float noLosStartY = 0.0f;
        uint32 lastGiveUpMs = 0;
        uint32 giveUpsInARow = 0;

        // Second give-up clock, anchored on the bot's own position instead of the target's
        // distance (see Execute).
        uint32 stoodSinceMs = 0;
        float stoodX = 0.0f;
        float stoodY = 0.0f;
        ObjectGuid stoodTarget;
        float stoodDist = 0.0f;
        uint32 lastExecuteMs = 0;

        static uint32 const kNoHeadwayMs = 15000;
        static uint32 const kNoMoveMs = 10000;
        static uint32 const kStandResetMs = 2000;

        // A creature that is in evade mode, or that the core has flagged as unable to reach
        // the bot (Creature::IsEvadeBecauseTargetNotReachable), will not come to us and cannot
        // be damaged where it stands - so the "it is attacking me, it will come" exception in
        // Execute does not hold for it. Measured on the frozen pool: a bot held the same Murloc
        // Streamrunner for 15+ minutes with victim=1, inLos=1, notreach=1, botmove=0, hp=100.
        static bool TargetCannotBeReached(Unit* target)
        {
            if (!target || !target->IsCreature())
                return false;

            Creature* creature = static_cast<Creature*>(target);
            return creature->IsInEvadeMode() || creature->IsEvadeBecauseTargetNotReachable();
        }

        // A given-up target must not survive as the bot's order. While "current target" or
        // "attack target" still points at a live hostile, CombatEndTrigger keeps the bot in
        // BOT_STATE_COMBAT and the combat engine has no travel/grind/loot actions, so a bot
        // wedged on a creature it cannot reach never travels again (measured: 81 bots stood
        // inside a 0.5 yd box for 20-124 minutes while still ordering attacks). Clearing the
        // order lets "combat end" fire and the non-combat engine pick a reachable target.
        void DropAttackTarget()
        {
            context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
            context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
            context->GetValue<Unit*>("current target")->Set(nullptr);
            bot->AttackStop();
        }

    public:

        virtual void Qualify(const std::string& qualifier) override
        {
            Qualified::Qualify(qualifier);

            // Get the distance from the qualified spell given
            if (!qualifier.empty())
            {
                spellName = Qualified::getMultiQualifierStr(qualifier, 0, "::");

                float maxSpellRange;
                if (ai->GetSpellRange(spellName, &maxSpellRange))
                {
                    range = maxSpellRange;
                }
            }
        }

        virtual bool Execute(Event& event) override
		{
            Unit* target = GetTarget();
            if (target)
            {
                UpdateMovementState();

                // Ignore movement if too far
                const float distanceToTarget = bot->GetDistance(target);
                float chaseDist = range;
                const bool inLos = bot->IsWithinLOSInMap(target, true);
                const bool isFriend = sServerFacade.IsFriendlyTo(bot, target);

                // Stand-still clock: counts time in which neither the bot nor its target made
                // headway, and is re-anchored on everything that makes "not moving" mean nothing -
                // a different reach target, a gap since the last reach attempt (the bot was
                // resting, looting, travelling or in another engine), the bot being held in place
                // or busy (rooted, stunned/feared/confused, sitting, casting), the bot taking a
                // step, or the target closing two yards. Without those resets a bot that stood
                // still for 10 s while eating would give up on the first tick of its next pull.
                {
                    uint32 const nowMs = WorldTimer::getMSTime();
                    bool const idleGap = lastExecuteMs == 0 || nowMs - lastExecuteMs > kStandResetMs;
                    bool const held = bot->IsRooted() || bot->HasUnitState(UNIT_STAT_NO_FREE_MOVE) ||
                        bot->GetStandState() != UNIT_STAND_STATE_STAND ||
                        bot->IsNonMeleeSpellCasted(true, false, true);
                    float const dxStood = bot->GetPositionX() - stoodX;
                    float const dyStood = bot->GetPositionY() - stoodY;
                    bool const botMoved = (dxStood * dxStood + dyStood * dyStood) > 4.0f;
                    bool const targetClosed = stoodDist > 0.0f && (stoodDist - distanceToTarget) > 2.0f;
                    if (stoodSinceMs == 0 || idleGap || held || botMoved || targetClosed ||
                        stoodTarget != target->getObjectGuid())
                    {
                        stoodSinceMs = nowMs;
                        stoodX = bot->GetPositionX();
                        stoodY = bot->GetPositionY();
                        stoodDist = distanceToTarget;
                        stoodTarget = target->getObjectGuid();
                    }
                    lastExecuteMs = nowMs;
                }

                if (range > 0.0f)
                {
                    // Move to 75% of max range so we land comfortably inside the
                    // range envelope rather than at its edge. This prevents constant
                    // fidgeting when the target drifts slightly out of max range, and
                    // stops bots from walking all the way out to the range limit before
                    // they cast. The 0.75 factor keeps a buffer for target movement
                    // while still being well within casting distance.
                    const float preferredDist = range * 0.75f;
                    chaseDist = inLos ? preferredDist : (isFriend ? std::min(distanceToTarget * 0.9f, preferredDist) : preferredDist);
                    chaseDist = std::max(chaseDist - sPlayerbotAIConfig.contactDistance, 0.0f);
                }

                if (MoveStyleValue::WaitForEnemy(ai) && target->m_movementInfo.HasMovementFlag(MOVEFLAG_MASK_MOVING) &&
                        sServerFacade.isInFront(target, bot, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT) &&
                        sServerFacade.IsDistanceGreaterThan(distanceToTarget, sPlayerbotAIConfig.tooCloseDistance))
                {
                    return true;
                }

                // A hostile target that stays out of line of sight while the bot makes no
                // headway towards it will not be reached from here (a kobold inside a mine,
                // the bot on the hill above it - measured: 57 such episodes of 6 minutes on
                // average in one night, ended only by "combat long stuck"). Headway is
                // getting closer or walking a detour around the obstacle; waiting for an
                // approaching enemy (above) does not count either way. Without headway for
                // 15 s the target goes on the "unreachable targets" list for five minutes
                // (honoured by AttackersValue::IgnoreTarget) and "invalid target" selects
                // something else. A target that is attacking the bot is never given up on
                // while it can reach the bot at all: it is reachable, or it will come to us.
                // A creature in evade mode, or one the core has flagged as unable to reach the
                // bot, is the exception (see TargetCannotBeReached); and a target the bot has
                // not walked towards for kNoMoveMs while out of its reach is given up on as
                // well, whatever the creature does.
                // Measured after the first version (300 bots, one hour): the give-up only fired
                // without line of sight, so a target in plain sight across a fence, a cliff or a
                // stream was chased for minutes ("inLos=true, 54 yd, 100 s"); and the list was
                // per creature, so a mine full of kobolds was given up on one at a time. Now:
                // no headway for 15 s counts with or without line of sight (a rooted or stunned
                // bot is not judged), and the kind of creature is given up on as well.
                if (!isFriend && !bot->IsRooted() && !bot->HasUnitState(UNIT_STAT_NO_FREE_MOVE))
                {
                    uint32 const nowMs = WorldTimer::getMSTime();
                    float const dxStart = bot->GetPositionX() - noLosStartX;
                    float const dyStart = bot->GetPositionY() - noLosStartY;
                    bool const detour = (dxStart * dxStart + dyStart * dyStart) > 8.0f * 8.0f;
                    if (noLosTarget != target->getObjectGuid() || distanceToTarget < noLosBestDist - 2.0f || detour)
                    {
                        noLosTarget = target->getObjectGuid();
                        noLosSinceMs = nowMs;
                        noLosBestDist = distanceToTarget;
                        noLosStartX = bot->GetPositionX();
                        noLosStartY = bot->GetPositionY();
                    }

                    // The clock above restarts whenever the creature wanders two yards closer or
                    // the order moves to another creature of the same kind - on a field of wandering
                    // mobs it can go an hour without firing (measured: repeats=3..14 on one creature,
                    // orders spread over 50 minutes). The stand-still clock anchored in Execute looks
                    // only at the bot, so "not a single step for kNoMoveMs while the target is out of
                    // the bot's reach and the bot is not fighting it" is a give-up of its own. That is
                    // the shape of the frozen pool: the chase path came back empty (the core's chase
                    // generator returns without launching a spline when the path to the target is
                    // NOPATH or incomplete in sight) while ChaseTo still reported success, so nothing
                    // else ever noticed the bot was not moving.
                    bool const cannotBeReached = TargetCannotBeReached(target);
                    // A creature that is running (feared or fleeing at low health) is a live fight,
                    // not a stuck spot: the bot keeps after it instead of abandoning a nearly dead
                    // mob and blacklisting its kind.
                    bool const runner = target->HasUnitState(UNIT_STAT_FLEEING | UNIT_STAT_CONFUSED);
                    bool const noHeadway = !runner && nowMs - noLosSinceMs >= kNoHeadwayMs &&
                        (target->GetVictim() != bot || !inLos || cannotBeReached);
                    bool const outOfReach = range > 0.0f
                        ? distanceToTarget > range * 1.4f
                        : !bot->CanReachWithMeleeAutoAttack(target);
                    bool const fighting = !cannotBeReached && (bot->IsInCombat() || target->GetVictim() == bot);
                    bool const stoodStill = !runner && outOfReach && !fighting && nowMs - stoodSinceMs >= kNoMoveMs;

                    if (noHeadway || stoodStill)
                    {
                        // The attacker exception only holds in sight: a mob that "attacks" the bot
                        // from out of sight for 15 s without closing in is stuck as well (a Wendigo in
                        // its cave kept a mage as its victim for four minutes at 44 yd).
                        std::map<ObjectGuid, uint32>& unreachable = context->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets")->Get();
                        auto const known = unreachable.find(target->getObjectGuid());
                        bool const alreadyGivenUp = known != unreachable.end() && nowMs < known->second;
                        unreachable[target->getObjectGuid()] = nowMs + 5 * MINUTE * IN_MILLISECONDS;
                        if (target->IsCreature())
                            context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[target->GetEntry()] = nowMs + 5 * MINUTE * IN_MILLISECONDS;

                        // Drop the order with it: the blacklist only steers the next pick, while the
                        // order the bot already holds keeps it in combat with a creature it cannot
                        // reach. Clearing it lets CombatEndTrigger fire and the non-combat engine
                        // (travel, grind, loot) run again.
                        DropAttackTarget();

                        if (alreadyGivenUp)
                        {
                            // A second reach action (melee and spell both track the target) - one
                            // give-up is enough, no second log row and no second anomaly.
                            noLosTarget = ObjectGuid();
                            return false;
                        }
                        ai->TellDebug(GetMaster(), "Giving up on " + std::string(target->GetName()) +
                            (stoodStill && !noHeadway ? " - out of reach, not a step taken for 10 s"
                                : inLos ? " - in sight but no headway for 15 s" : " - out of line of sight, no headway for 15 s"), "debug move");

                        // Second give-up within three minutes: the whole spot is bad (a cave mouth with
                        // troggs, trolls and wolves - measured: one bot gave up on six kinds in twenty
                        // minutes and never left). Every hostile kind in sight goes on the list, so the
                        // grind destinations here drop and the bot moves on.
                        if (lastGiveUpMs && nowMs - lastGiveUpMs <= 3 * MINUTE * IN_MILLISECONDS)
                            ++giveUpsInARow;
                        else
                            giveUpsInARow = 1;
                        lastGiveUpMs = nowMs;
                        if (giveUpsInARow >= 2)
                        {
                            giveUpsInARow = 0;
                            std::map<uint32, uint32>& kinds = context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get();
                            uint32 blocked = 0;
                            std::list<ObjectGuid> inSight = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
                            for (ObjectGuid const& guid : inSight)
                            {
                                Unit* unit = ai->GetUnit(guid);
                                if (!unit || !unit->IsCreature() || !sServerFacade.IsHostileTo(bot, unit))
                                    continue;
                                kinds[unit->GetEntry()] = nowMs + 5 * MINUTE * IN_MILLISECONDS;
                                ++blocked;
                            }
                            ai->TellDebug(GetMaster(), "Leaving this spot - second give-up within three minutes, " + std::to_string(blocked) + " kinds set aside", "debug move");
                            if (sPlayerbotAIConfig.hasLog("unreachable_targets.csv"))
                            {
                                time_t const nowSpot = time(nullptr);
                                char stampSpot[32];
                                strftime(stampSpot, sizeof(stampSpot), "%Y-%m-%d %H:%M:%S", localtime(&nowSpot));
                                std::ostringstream outSpot;
                                outSpot << stampSpot << "," << bot->GetName() << "," << bot->GetLevel() << ",SPOT," << blocked << ",leave";
                                sPlayerbotAIConfig.log("unreachable_targets.csv", outSpot.str().c_str());
                            }
                        }
                        if (sPlayerbotAIConfig.hasLog("unreachable_targets.csv"))
                        {
                            time_t const now = time(nullptr);
                            char stamp[32];
                            strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
                            std::ostringstream out;
                            out << stamp << "," << bot->GetName() << "," << bot->GetLevel() << "," << target->GetName() << "," << (int)distanceToTarget << "," << (inLos ? "los" : "nolos");
                            sPlayerbotAIConfig.log("unreachable_targets.csv", out.str().c_str());
                        }
                        noLosTarget = ObjectGuid();
                        stoodSinceMs = 0;
                        return false;
                    }
                }
                else
                {
                    noLosTarget = ObjectGuid();
                }

                if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "Moving to reach " << ChatHelper::formatWorldobject(target);
                    ai->TellPlayerNoFacing(GetMaster(), out);
                }

                if (inLos && isFriend && (range <= ai->GetRange("follow")))
                {
                    return MoveNear(target, chaseDist);
                }
                else
                {
                    return ChaseTo(target, chaseDist, bot->GetAngle(target));
                }
            }

            return false;
        }

        // True for reach actions whose target is always meant to be attacked
        // (melee, pull) - false for "reach spell", which is also used to close
        // distance on friendly targets (e.g. Blessing of Protection/Freedom).
        virtual bool RequiresAttackableTarget() const { return false; }

        virtual bool isUseful() override
		{
            // Do not move if stay strategy is set
            if (!ai->HasStrategy("stay", ai->GetState()))
            {
                Unit* target = GetTarget();
                if (target)
                {
                    // A target that can never legally be attacked (friendly, wrong
                    // phase, etc.) isn't worth closing distance to - this is what let
                    // bots walk up to and cluster around friendly NPCs once a stale
                    // "current target"/"pull target" pointed at one (same check used
                    // in PossibleAttackTargetsValue::IsPossibleTarget).
                    if (RequiresAttackableTarget() && !bot->IsValidAttackTarget(target))
                    {
                        return false;
                    }

                    // Do not move while casting
                    if (!bot->IsNonMeleeSpellCasted(true, false, true))
                    {
                        // Check if the spell for which the reach action is used for can be casted
                        if (!spellName.empty() && !ai->CanCastSpell(spellName, target, true, nullptr, true, true, true))
                        {
                            return false;
                        }

                        // Force move if not in los
                        if (bot->IsWithinLOSInMap(target, true))
                        {
                            // Check if the bot is already on the range required
                            return bot->GetDistance(target) > range;
                        }

                        return true;
                    }
                }
            }

            return false;
        }

        virtual std::string GetTargetName() override { return "current target"; }
        std::string GetSpellName() const { return spellName; }

        virtual Unit* GetTarget() override
        {
            // Get the target from the qualifiers
            if (!qualifier.empty())
            {
                std::string targetQualifier;
                const std::string targetName = Qualified::getMultiQualifierStr(qualifier, 1, "::");
                if (targetName != "current target")
                {
                    targetQualifier = Qualified::getMultiQualifierStr(qualifier, 2, "::");
                }

                return targetQualifier.empty() ? AI_VALUE(Unit*, targetName) : AI_VALUE2(Unit*, targetName, targetQualifier);
            }
            else
            {
                return AI_VALUE(Unit*, GetTargetName());
            }
        }

    protected:
        float range;
        std::string spellName;
    };

    class CastReachTargetSpellAction : public CastSpellAction
    {
    public:
        CastReachTargetSpellAction(PlayerbotAI* ai, std::string spell, float distance) : CastSpellAction(ai, spell), distance(distance) {}

		virtual bool isUseful() override
		{
            // Do not move if stay strategy is set
            if (ai->HasStrategy("stay", ai->GetState()))
                return false;

			return sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "current target"), (distance + sPlayerbotAIConfig.contactDistance));
		}

    protected:
        float distance;
    };

    class ReachMeleeAction : public ReachTargetAction
	{
    public:
        ReachMeleeAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach melee") {}
        bool RequiresAttackableTarget() const override { return true; }
    };

    class ReachSpellAction : public ReachTargetAction
	{
    public:
        ReachSpellAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach spell", ai->GetRange("spell")) {}
    };

    class ReachPullAction : public ReachTargetAction
    {
    public:
        ReachPullAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach pull")
        {
            PullStrategy* strategy = PullStrategy::Get(ai);
            if (strategy)
            {
                range = strategy->GetRange();
            }
        }

        void Qualify(const std::string& qualifier) override
        {
            ReachTargetAction::Qualify(qualifier);

            // Reduce the range slightly for a more accurate pull (moving targets can get out of reach)
            const float threshold = 5.0f;
            range = range > threshold ? range - threshold : range;
        }

        std::string GetTargetName() override { return "pull target"; }
        bool RequiresAttackableTarget() const override { return true; }
    };

    class ReachPartyMemberToHealAction : public ReachTargetAction
    {
    public:
        ReachPartyMemberToHealAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach party member to heal", ai->GetRange("heal")) {}
        virtual std::string GetTargetName() override { return "party member to heal"; }
    };

    class ReachPartyMemberToResurrectAction : public ReachTargetAction
    {
    public:
        ReachPartyMemberToResurrectAction(PlayerbotAI* ai)
            : ReachTargetAction(ai, "reach party member to resurrect", ai->GetRange("spell")) {}
        virtual std::string GetTargetName() override { return "party member to resurrect"; }
    };

    class ReachPartyMemberForTotemAction : public ReachTargetAction
    {
    public:
        ReachPartyMemberForTotemAction(PlayerbotAI* ai)
            : ReachTargetAction(ai, "reach party member for totem", 20.0f) {}

        void Qualify(const std::string& qualifier) override
        {
            ReachTargetAction::Qualify(qualifier);
            range = 20.0f;
        }

        Unit* GetTarget() override
        {
            std::string totemSpell = spellName;
            if (totemSpell.empty() && !qualifier.empty())
                totemSpell = Qualified::getMultiQualifierStr(qualifier, 0, "::");

            Group* group = bot->GetGroup();
            if (!group || totemSpell.empty())
                return AI_VALUE(Unit*, "master target");

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

                if (!ai->HasAura(totemSpell, member, false, true))
                    return member;
            }

            return nullptr;
        }
    };
}
