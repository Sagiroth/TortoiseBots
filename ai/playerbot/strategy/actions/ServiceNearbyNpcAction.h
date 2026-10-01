#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"

namespace ai
{
    // Walks to the class trainer or vendor the bot has an idle-time reason to
    // use - see NearbyServiceTarget() in MaintenanceValues.h - and then runs
    // the existing maintenance action for it ("sell" or "trainer"). This is the
    // rule that makes a bot standing next to the NPC that would unblock it use
    // it, instead of starting a journey that may never finish (issue #379).
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
            return "This action is used by an idle bot standing near a class trainer or vendor it needs: it walks to the NPC and\n"
                "then either sells its vendor junk or learns the class ranks it can afford.";
        }
        virtual std::vector<std::string> GetUsedActions() { return { "sell", "trainer" }; }
        virtual std::vector<std::string> GetUsedValues() { return { "should service nearby npc", "nearby service target" }; }
#endif
    };
}
