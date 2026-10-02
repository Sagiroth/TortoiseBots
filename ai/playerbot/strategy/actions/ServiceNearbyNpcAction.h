#pragma once
#include <cstdint>
#include <string>
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/NearbyServicePolicy.h"

namespace ai
{
    class GuidPosition;

    // Walks to the quest giver, class trainer or vendor the bot has an idle-time
    // reason to use - see NearbyServiceTarget() in MaintenanceValues.h - and then
    // runs the existing action for it ("talk to quest giver", "accept all
    // quests", "sell" or "trainer"). This is the rule that makes a bot standing
    // next to the NPC that would unblock it use it, instead of starting a
    // journey that may never finish (issue #379).
    class ServiceNearbyNpcAction : public MovementAction
    {
    public:
        ServiceNearbyNpcAction(PlayerbotAI* ai, std::string name = "service nearby npc") : MovementAction(ai, name) {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "service nearby npc"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action is used by an idle bot standing near a quest giver, class trainer or vendor it needs:\n"
                "it walks to the NPC and then hands in a finished quest, accepts a quest it can take, sells its vendor\n"
                "junk, or learns the class ranks it can afford.";
        }
        virtual std::vector<std::string> GetUsedActions() { return { "talk to quest giver", "accept all quests", "sell", "trainer" }; }
        virtual std::vector<std::string> GetUsedValues() { return { "should service nearby npc", "nearby service target" }; }
#endif

    private:
        // One verb attempt in ranking order; false when the verb does not apply,
        // is parked, or failed. A parked verb is skipped so the NPC's remaining
        // verbs still run.
        bool TryVerb(Event& event, GuidPosition target, NearbyServiceKind kind, std::string const& verb);
        // Runs one verb and logs the NearbyService row when it did something.
        // Failures accumulate in the fixed-size fail parks (load/mutate/store -
        // AI_VALUE returns a copy); a success clears the pair.
        bool RunVerb(uint64_t npcGuid, int verbId,
            std::string const& kind, std::string const& action, Event event, uint32 npcEntry, time_t now);
    };
}
