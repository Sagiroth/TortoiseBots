
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "ReactionStrategy.h"
#include "playerbot/ReactionRowsPolicy.h"

using namespace ai;

// The rows' priorities are the policy header's constant plus the offsets they always carried; ACTION_EMERGENCY is
// the engine's own value, so a change to one is a build error, not a silent re-prioritisation.
static_assert(kReactionRaidEmergency == static_cast<int>(ACTION_EMERGENCY),
    "ReactionRowsPolicy.h's priority base must be ACTION_EMERGENCY (strategy/Strategy.h)");

void ReactionStrategy::InitReactionTriggers(std::list<TriggerNode*> &triggers)
{
    for (ReactionRaidRow const& row : ReactionRaidRows(sPlayerbotAIConfig.raidReactions))
    {
        triggers.push_back(new TriggerNode(
            row.trigger,
            NextAction::array(0, new NextAction(row.action, row.priority), NULL)));
    }
}
