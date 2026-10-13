#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AvoidAoePolicy.h"
#include "playerbot/CreatureAuraKeepoutPolicy.h"

namespace ai
{
    // List of hostile and neutral targets in a range around the bot
    class PossibleTargetsValue : public NearestUnitsValue, public Qualified
	{
	public:
        PossibleTargetsValue(PlayerbotAI* ai, std::string name = "possible targets", float range = sPlayerbotAIConfig.sightDistance, bool ignoreLos = false) :
          NearestUnitsValue(ai, name, range, ignoreLos), Qualified() { this->keepLegacyRefresh = true; }

        std::list<ObjectGuid> Calculate() override;
        static bool IsValid(Unit* target, Player* player, bool ignoreLos = false);

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
        // nullptr when none or with the toggle off. No new world scan.
        static Unit* KeepoutRingAt(PlayerbotAI* ai, float x, float y, Unit* extra = nullptr);

        // AiPlayerbot.AvoidCreatureAuras: true when this bot must not use a
        // gap closer (Charge, Intercept, Feral Charge) onto `target`
        // (CreatureAuraGapCloserVetoed): a non-tank bot without an attack
        // order on it, and the target is a listed creature or the landing
        // lies in a ring. The landing is checked against the cached list
        // first, then by a bounded lookup of listed creatures around the
        // target (the cast fires from up to 25 yd, past the 15 yd list).
        // Callers ask only when the cast is otherwise ready, never per tick.
        static bool GapCloserIntoKeepout(PlayerbotAI* ai, Unit* target);

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;
	};
}
