#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AvoidAoePolicy.h"

namespace ai
{
    // List of hostile and neutral targets in a range around the bot
    class PossibleTargetsValue : public NearestUnitsValue, public Qualified
	{
	public:
        PossibleTargetsValue(PlayerbotAI* ai, std::string name = "possible targets", float range = sPlayerbotAIConfig.sightDistance, bool ignoreLos = false) :
          NearestUnitsValue(ai, name, range, ignoreLos), Qualified() {}

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
    class PossibleTriggersValue : public NearestUnitsValue
	{
	public:
        PossibleTriggersValue(PlayerbotAI* ai) :
            NearestUnitsValue(ai, "possible triggers", kMaxAoeAvoidRadiusYd, true) {}

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;
	};
}
