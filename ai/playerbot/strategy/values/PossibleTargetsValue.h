#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AvoidAoePolicy.h"
#include "../../policy/loot/CreatureAuraKeepoutPolicy.h"

#include <atomic>

namespace ai
{
    // List of hostile and neutral targets in a range around the bot
    class PossibleTargetsValue : public NearestUnitsValue, public Qualified
	{
	public:
        PossibleTargetsValue(PlayerbotAI* ai, std::string name = "possible targets", float range = sPlayerbotAIConfig.sightDistance, bool ignoreLos = false) :
          NearestUnitsValue(ai, name, range, ignoreLos), Qualified() { this->keepLegacyRefresh = true; }

        std::list<ObjectGuid> Calculate() override;
        static bool IsValid(Unit* target, Player* player, bool ignoreLos = false, char const** reason = nullptr);

    protected:
        virtual void FindUnits(std::list<Unit*> &targets) override;
        virtual bool AcceptUnit(Unit* unit) override;

        static void FindPossibleTargets(Player* player, std::list<Unit*>& targets, float range);
        static bool IsFriendly(Unit* target, Player* player);
        static bool IsAttackable(Unit* target, Player* player);
	};

    class AllTargetsValue : public PossibleTargetsValue
	{
	public:
        AllTargetsValue(PlayerbotAI* ai, float range = sPlayerbotAIConfig.sightDistance) :
        PossibleTargetsValue(ai, "all targets", range, true) {}
	};

    // Proactive AoE sensor (donor PossibleTriggersValue shape): hostile,
    // not-selectable units within 15yd whose periodic-trigger aura fires a
    // school-damage spell (e.g. invisible void-zone trigger NPCs). LoS is
    // ignored — the trigger sits inside the zone it marks.
    // With AiPlayerbot.AvoidCreatureAuras on it also accepts a selectable
    // creature listed in CreatureAuraKeepoutPolicy.h that carries its aura
    // (Maraudon's Creeping Sludge), so the same sensor sees it.
    class PossibleTriggersValue : public NearestUnitsValue
	{
	public:
        PossibleTriggersValue(PlayerbotAI* ai) :
            NearestUnitsValue(ai, "possible triggers", kMaxAoeAvoidRadiusYd, true) { this->keepLegacyRefresh = true; }

        // The keep-out row for a live listed creature carrying its aura while
        // the toggle is on; nullptr otherwise. The explicit-order exemption is
        // per bot and stays with the callers.
        static CreatureAuraKeepout const* KeepoutFor(Unit* unit);

        // The listed creature whose ring (hazard radius, centre measure)
        // contains the point (x, y): `extra` first (a target the 15 yd list
        // may not reach yet), then the bot's cached "possible triggers" list.
        // The creature the player ordered this bot to attack is skipped.
        // `ignoreTankExempt` (a tank in combat) also skips the rows that give
        // tanks a pass (CreatureAuraKeepout::tankExempt); the same flag on
        // EndPointRinged and SnapshotRingedAt.
        // nullptr when none or with the toggle off. No new world scan.
        static Unit* KeepoutRingAt(PlayerbotAI* ai, float x, float y, Unit* extra = nullptr,
            bool ignoreTankExempt = false);

        // True when a move's end point (x, y, z) lies in a ring: `extra` and
        // the cached list (KeepoutRingAt, exact ring), then the shared hazard
        // snapshot (SnapshotRingedAt), which reaches rings past the 15 yd list
        // (set behind or rear flank on a target up to 15 yd away).
        static bool EndPointRinged(PlayerbotAI* ai, float x, float y, float z, Unit* extra = nullptr,
            bool ignoreTankExempt = false);

        // (x, y, z) lies in the ring of a listed creature the live checks do
        // not see (not in the bot's live list, not `skip`, not the explicit
        // attack target), from the shared hazard snapshot of the bot's
        // (map, instance, 30 yd cell): ring widened by the drift for the
        // snapshot's age, same floor only (CreatureAuraSnapshotRinged). At
        // most one bounded grid visit per cell per kCreatureAuraSnapshotMs and
        // kKeepoutRefreshesPerTick per world update. With no usable snapshot
        // (or a point outside its cover): false for an end point (the live
        // checks decide, as #882); with `landingLookup` (a gap-closer
        // landing), #882's one bounded lookup of listed creatures around the
        // point at their exact ring instead. Never looks on a map without a
        // listed row.
        static bool SnapshotRingedAt(PlayerbotAI* ai, float x, float y, float z, Unit* skip,
            bool landingLookup = false, bool ignoreTankExempt = false);

        // Once per world update, before the bots run: resets the snapshot
        // refresh budget.
        static void BeginKeepoutTick();

        // AiPlayerbot.AvoidCreatureAuras: true when this bot must not use a
        // gap closer (Charge, Intercept, Feral Charge) onto `target`
        // (CreatureAuraGapCloserVetoed): a non-tank bot without an attack
        // order on it, and the target is a listed creature or the landing
        // lies in a ring. The callers ask from isUseful(), which the engine
        // evaluates on every tick the trigger fires, so the landing checks run
        // only when `spell` could fire this tick (GapCloserCouldFire: known,
        // off cooldown, enough rage, Charge out of combat, within its min/max
        // range); otherwise there is nothing to veto and isPossible() refuses
        // the cast as it does with the option off. The landing is then checked
        // against the cached 15 yd list, then against the shared hazard snapshot
        // (the cast fires from up to 25 yd; shared by all bots and with the
        // end-point checks).
        static bool GapCloserIntoKeepout(PlayerbotAI* ai, std::string const& spell, Unit* target);

        // The cheap readiness test above (CreatureAuraGapCloserCouldFire).
        // Every reason it refuses is one CastSpellAction::isPossible refuses
        // too. Stance and form are left to the prerequisite.
        static bool GapCloserCouldFire(PlayerbotAI* ai, std::string const& spell, Unit* target);

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;

    public:
        // Grid visits made for the shared hazard snapshots (keepoutScans), and
        // lookups due a refresh after the world update's budget was spent
        // (keepoutBudgetMiss); BOTPERF reports and resets both.
        static std::atomic<uint32_t> hazardSnapshotRefreshCount;
        static std::atomic<uint32_t> hazardSnapshotBudgetMissCount;
	};
}
