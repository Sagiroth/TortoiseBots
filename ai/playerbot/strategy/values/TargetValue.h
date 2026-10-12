#pragma once
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ChatHelper.h"
#include "playerbot/strategy//AiObject.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelMgr.h"
#include <map>

namespace ai
{
    class FindTargetStrategy
    {
    public:
        FindTargetStrategy(PlayerbotAI* ai)
        {
            result = NULL;
            this->ai = ai;
        }

    public:
        Unit* GetResult() { return result; }

    public:
        virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) = 0;
        void GetPlayerCount(Unit* creature, int* tankCount, int* dpsCount);

    protected:
        Unit* result;
        PlayerbotAI* ai;

    protected:
        std::map<Unit*, int> tankCountCache;
        std::map<Unit*, int> dpsCountCache;
    };

    class FindNonCcTargetStrategy : public FindTargetStrategy
    {
    public:
        FindNonCcTargetStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai) {}

        // True when every live enemy fighting the group is a CC mark (Moon
        // or a bot's assigned "rti cc" mark), not counting `except`. Judged
        // from the raw "attackers" list, so a stunned, rooted, feared or
        // sheeped unmarked mob still counts as an enemy left to fight.
        bool OnlyCcMarksLeft(Unit* except = nullptr);

        // The unit is crowd-controlled right now: a damage-breakable CC
        // (sheep, sap, gouge, shackle, hibernate, trap, seduction, ...), a
        // fear or a banish. Frost Nova's root is not crowd control here.
        static bool IsCrowdControlled(PlayerbotAI* ai, Unit* unit);
        // `caster`'s cast bar is a crowd-control spell aimed at `target`.
        static bool IsCastingCcAt(Unit* caster, Unit* target);
        // The spell carries a crowd-control mechanic (polymorph, banish,
        // fear, sleep, knockout/sap, shackle) on the spell or an effect.
        static bool IsCcSpell(SpellEntry const* info);

    protected:
        // Moon or a bot's assigned "rti cc" mark, crowd-controlled or not.
        bool IsCcMark(Unit* attacker);
        // A CC mark the party leaves alone: a mark that is crowd-controlled,
        // has a group member's CC cast bar on it, is not fighting yet, or is
        // in its hold window (CcMarkHoldPolicy.h, first 10 s of combat) with
        // its CC bot ready and in range. A free mark (CC missed, broke, no
        // CC bot, or the window ran out) is a normal enemy.
        virtual bool IsCcTarget(Unit* attacker);
        bool IsCcProtected(Unit* unit);
        // `player` (a bot with AI `memberAi`) can CC `unit` now: "cc" on,
        // knows a fitting cast-time CC spell off cooldown, within spell
        // range + 5 yd. Whether `unit` is its mark is the caller's test.
        static bool IsCcBotReadyFor(PlayerbotAI* memberAi, Player* player, Unit* unit);

    };

    // Crowd control the party must not break, checked outside a target
    // tournament (current target, snare/DoT picks, splash). Build one per
    // decision: the "only CC marks left" answer is computed once, on need.
    class CcGuard : public FindNonCcTargetStrategy
    {
    public:
        CcGuard(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai), onlyMarksLeft(-1) {}
        void CheckAttacker(Unit* /*attacker*/, ThreatManager* /*threatManager*/) override {}

        // A protected CC mark (see IsCcTarget) while any other enemy still
        // fights the group: nobody targets, taunts, DoTs or splashes it (it
        // is killed last, see TargetValue::FindLastCcTarget).
        bool IsHeldCcMark(Unit* unit);
        // This bot's own assigned CC mark ("rti cc target") that it can CC
        // now (IsCcBotReadyFor), held or free.
        bool IsOwnReadyCcMark(Unit* unit);
        // A group member's cast bar is a CC spell aimed at `unit`.
        bool GroupCastsCcAt(Unit* unit);
        // An enemy fighting the group within `radius` of `center` (bounding
        // radii included) that holds a damage-breakable CC, or with
        // `includeMarks` is a held CC mark (being or about to be CC'd), or
        // is this bot's own ready CC mark while other enemies are left (the
        // CC bot never splashes the mob it will sheep next). `skip` (may be
        // null) is not counted (CcGuardCountsUnit): Blade Flurry passes the
        // rogue's own victim, which its proc never hits.
        bool ProtectedEnemyNear(WorldObject* center, float radius, bool includeMarks, Unit* skip = nullptr);
        // A CC mark fighting the group within `radius` of `center` whose CC
        // bot (the group bot with it as "rti cc target") can CC it now
        // (IsCcBotReadyFor), at any combat time, while other enemies are
        // left. Only Blade Flurry's cancel uses it (CcCancelBladeFlurry).
        bool ReadyCcMarkNear(WorldObject* center, float radius);

    private:
        bool OnlyMarksLeftCached();
        int onlyMarksLeft;
    };

    class TargetValue : public UnitCalculatedValue
	{
	public:
        TargetValue(PlayerbotAI* ai, std::string name = "target", int checkInterval = 1) : UnitCalculatedValue(ai, name, checkInterval) {}
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "target"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "target"; }
        virtual std::string GetHelpDescription() { return "This value contains a [h:object|unit] which is the best target based on the current target strategy."; }
        virtual std::vector<std::string> GetUsedValues() { return { "possible attack targets" }; }
#endif
    protected:
        Unit* FindTarget(FindTargetStrategy* strategy);
        // A human attack command stores its target separately from the
        // transient current/dps/tank values.  Target selectors use this helper
        // to preserve that commitment while the normal combat engine catches
        // up and creates threat.
        Unit* GetExplicitAttackTarget();
        // Kill-last fallback for the tank/DPS pickers: once only CC-marked
        // mobs (Moon or a bot's assigned "rti cc" mark) are left fighting the
        // group, sheeped or free, the first live one in combat is the target
        // instead of none. Any other enemy, crowd-controlled or not, blocks it.
        Unit* FindLastCcTarget();
    };

    class RpgTargetValue : public ManualSetValue<GuidPosition>
    {
    public:
        RpgTargetValue(PlayerbotAI* ai, std::string name = "rpg target") : ManualSetValue<GuidPosition>(ai, GuidPosition(), name) {}
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "rpg target"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "rpg"; }
        virtual std::string GetHelpDescription() { return "This value contains the [h:object|objectGuid] of a [h:object|unit] or [h:object|gameObject] to move to and rpg with.\nThis value is manually set."; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif

        virtual std::string Format() override
        {
            return chat->formatGuidPosition(value, bot);
        }
    };

    class TravelTargetValue : public ManualSetValue<TravelTarget*>
    {
    public:
        TravelTargetValue(PlayerbotAI* ai, std::string name = "travel target") : ManualSetValue<TravelTarget*>(ai, new TravelTarget(ai), name) {}
        virtual ~TravelTargetValue() override { delete value; }
    };

    class LeaderTravelTargetValue : public CalculatedValue<TravelTarget*>
    {
    public:
        LeaderTravelTargetValue(PlayerbotAI* ai, std::string name = "leader travel target") : CalculatedValue<TravelTarget*>(ai, name) {};

        virtual TravelTarget* Calculate() override;
    };

    class IgnoreRpgTargetValue : public ManualSetValue<std::set<ObjectGuid>& >
    {
    public:
        IgnoreRpgTargetValue(PlayerbotAI* ai) : ManualSetValue<std::set<ObjectGuid>& >(ai, data, "ignore rpg targets") {}

    private:
        std::set<ObjectGuid> data;
    };

    // Targets a reach action gave up on - out of line of sight with no headway towards
    // them (see ReachTargetAction) - mapped to the time (ms) until which they are left
    // alone. Honoured by AttackersValue::IgnoreTarget, so neither the current target nor
    // the next grind pick lands on them again right away.
    class UnreachableTargetsValue : public ManualSetValue<std::map<ObjectGuid, uint32>& >
    {
    public:
        UnreachableTargetsValue(PlayerbotAI* ai) : ManualSetValue<std::map<ObjectGuid, uint32>& >(ai, data, "unreachable targets") {}

    private:
        std::map<ObjectGuid, uint32> data;
    };

    // The kinds (creature entries) a reach action gave up on, mapped to the time (ms) until
    // which the bot leaves that kind alone. A spot full of the same creatures - a mine full
    // of kobolds - was otherwise given up on one creature at a time (measured: one bot gave
    // up on Kobold Worker 72 times in an hour and never left the hill). Honoured by
    // AttackersValue::IgnoreTarget and by GrindTravelDestination::IsActive, so the grind
    // destination for that kind drops too and the bot moves on.
    class UnreachableEntriesValue : public ManualSetValue<std::map<uint32, uint32>& >
    {
    public:
        UnreachableEntriesValue(PlayerbotAI* ai) : ManualSetValue<std::map<uint32, uint32>& >(ai, data, "unreachable entries") {}

    private:
        std::map<uint32, uint32> data;
    };

    class TalkTargetValue : public ManualSetValue<ObjectGuid>
    {
    public:
        TalkTargetValue(PlayerbotAI* ai, std::string name = "talk target") : ManualSetValue<ObjectGuid>(ai, ObjectGuid(), name) {}
    };

    class AttackTargetValue : public ManualSetValue<ObjectGuid>
    {
    public:
        AttackTargetValue(PlayerbotAI* ai, std::string name = "attack target") : ManualSetValue<ObjectGuid>(ai, ObjectGuid(), name) {}
    };

    class ExplicitAttackTargetValue : public ManualSetValue<ObjectGuid>
    {
    public:
        ExplicitAttackTargetValue(PlayerbotAI* ai, std::string name = "explicit attack target")
            : ManualSetValue<ObjectGuid>(ai, ObjectGuid(), name) {}
    };

    class PullTargetValue : public UnitManualSetValue
    {
    public:
        PullTargetValue(PlayerbotAI* ai, std::string name = "pull target") : UnitManualSetValue(ai, nullptr, name) {}
        void Set(Unit* unit) override;
        Unit* Get() override;

    private:
        ObjectGuid guid;
    };

    class FollowTargetValue : public UnitCalculatedValue
    {
    public:
        FollowTargetValue(PlayerbotAI* ai, std::string name = "follow target") : UnitCalculatedValue(ai, name) {}
        Unit* Calculate() override;
    };

    class ManualFollowTargetValue : public GuidPositionManualSetValue
    {
    public:
        ManualFollowTargetValue(PlayerbotAI* ai, std::string name = "manual follow target") : GuidPositionManualSetValue(ai, GuidPosition(), name) {}
    };

    class ClosestAttackerTargetingMeTargetValue : public UnitCalculatedValue
    {
    public:
        ClosestAttackerTargetingMeTargetValue(PlayerbotAI* ai, std::string name = "closest attacker targeting me") : UnitCalculatedValue(ai, name) {}
        Unit* Calculate() override;
    };

    class FriendlyManualTargetsValue : public ManualSetValue<std::list<ObjectGuid>>
    {
    public:
        FriendlyManualTargetsValue(PlayerbotAI* ai) : ManualSetValue<std::list<ObjectGuid>>(ai, {}) {}
        std::list<ObjectGuid> Get() override;
        std::list<ObjectGuid> LazyGet() override;
    };

    class FocusHealTargetValue : public FriendlyManualTargetsValue
    {
    public:
        FocusHealTargetValue(PlayerbotAI* ai) : FriendlyManualTargetsValue(ai) {}
    };

    class BoostTargetsValue : public FriendlyManualTargetsValue
    {
    public:
        BoostTargetsValue(PlayerbotAI* ai) : FriendlyManualTargetsValue(ai) {}
    };

    class ReviveTargetsValue : public FriendlyManualTargetsValue
    {
    public:
        ReviveTargetsValue(PlayerbotAI* ai) : FriendlyManualTargetsValue(ai) {}
    };

    class BuffTargetsValue : public FriendlyManualTargetsValue
    {
    public:
        BuffTargetsValue(PlayerbotAI* ai) : FriendlyManualTargetsValue(ai) {}
    };
}

using ai::TargetValue;
