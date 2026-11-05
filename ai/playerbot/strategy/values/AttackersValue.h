#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"

namespace ai
{
    // List of hostile targets that are in combat with the bot (or bot group)
    class AttackersValue : public ObjectGuidListCalculatedValue, public Qualified
	{
        struct AddGuardiansHelper
        {
            explicit AddGuardiansHelper(std::set<Unit*>& units) : units(units) {}
            void operator()(Unit* target) const
            {
                units.insert(target);
            }

            std::set<Unit*>& units;
        };

	public:
        AttackersValue(PlayerbotAI* ai, std::string name = "attackers", int interval = 2) : ObjectGuidListCalculatedValue(ai, name, interval), Qualified() { this->keepLegacyRefresh = true; }
        virtual std::list<ObjectGuid> Calculate() override;

        static bool IsValid(Unit* target, Player* player, Player* owner = nullptr, bool checkInCombat = true, bool validatePossibleTarget = true);
        static bool IgnoreTarget(Unit* target, Player* playerToCheckAgainst);
        // AiPlayerbot.DungeonRunCharmYield: true while `target` is a member of `player`'s group/raid that a creature
        // currently charms (Hakkar's Cause Insanity 24327). Every pick and attack path asks this before it takes the
        // member: the charm makes it hostile, and the raid killed its own charmed damage dealers in 3-7 s without it.
        // A player charmer and a member of another group are ordinary enemies; healing the member stays allowed.
        static bool CharmedAllyOffLimits(Unit* target, Player* player);
        // True while `bot`'s own "keepout set aside" book refuses `target`
        // (a melee chase held outside a creature aura keep-out set it aside,
        // MovementAction::HoldChaseOutsideKeepout; CreatureAuraSetAsideHolds
        // decides). Asked by IgnoreTarget and by every pick path that
        // bypasses the attackers list (group tank's victim, raid marks).
        // Drops an entry that no longer holds.
        static bool IsKeepoutSetAside(Unit* target, Player* bot);
        // A mob running from the fight: the fleeing state (fear-style flee,
        // the timed flee) or the core's flee-for-assistance walk and its pause
        // at the helper (Creature::DoFleeToGetAssistance).
        static bool IsRunningMob(Unit* unit);
        // How far `target`, or the end of its run (its spline's last point),
        // lies below `bot`'s floor (RunnerFinishPolicy RunnerDropYd).
        static float RunnerDropYd(Unit* target, Player* bot);
        // Chases stop at ledges (RunnerFinishPolicy LetRunnerGoAtLedge, owner
        // decision 2026-10-23, scoped by the review of 565751aa): true while
        // `bot`, bound to an active dungeon run, lets `target` go: a running
        // unit more than 6 yd below its floor (or running there) whose navmesh
        // way has a cliff or is incomplete; never the bot's explicit attack
        // target (that clears the entry). Kept in the bot's own "runners let
        // go" book (purged and capped on every access) while it stays below.
        // Asked by IgnoreTarget (the attackers list) and
        // PossibleAttackTargetsValue::IsValid (every pick path that bypasses
        // that list, and the current target's check).
        static bool RunnerLetGoAtLedge(Unit* target, Player* bot);
        virtual std::string Format() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "attackers"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "combat"; }
        virtual std::string GetHelpDescription()
        {
            return "This value contains all the units attacking the player, it's group or those the player or group is attacking.";
        }
        virtual std::vector<std::string> GetUsedValues() { return {"possible targets", "current target" , "attack target" ,  "pull target" }; }
#endif

	private:
        void AddTargetsOf(Group* group, std::set<Unit*>& targets, std::set<ObjectGuid>& invalidTargets, bool getOne = false);
        void AddTargetsOf(Player* player, std::set<Unit*>& targets, std::set<ObjectGuid>& invalidTargets, bool getOne = false);
        static float GetRange() { return sPlayerbotAIConfig.sightDistance; }

        static bool InCombat(Unit* target, Player* player, bool checkPullTargets = true);

        WorldPosition calculatePos;

        // A target copied from another bot's list counts when it threatens us
        // or anyone we fight for (group, master) - the same rule a fresh scan
        // applies. Validating against ourselves only dropped every threat aimed
        // at a groupmate, so grouped bots never engaged (#549).
        bool IsValidForGroup(Unit* target);
    };

    // List of attackers that are currently targeting the bot
    class AttackersTargetingMeValue : public ObjectGuidListCalculatedValue
    {
    public:
        AttackersTargetingMeValue(PlayerbotAI* ai, std::string name = "attackers targeting me") : ObjectGuidListCalculatedValue(ai, name) {}
        std::list<ObjectGuid> Calculate() override;
    };
}
