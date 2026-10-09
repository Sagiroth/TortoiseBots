#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "QuestAction.h"

namespace ai
{
    class CheckValuesAction : public Action
    {
    public:
        CheckValuesAction(PlayerbotAI* ai);
        // Pool-only skip (perf): the six grid lists this warms all recompute
        // when now - lastCheck >= checkInterval/2 = 1 s, while a budgeted pool
        // visit lands ~26 s after the previous one - so the warming never
        // survives to the next visit, and nothing reads them later in this
        // one (Execute runs last, then the queue only drains). Every reader
        // recomputes on demand via Get(). Owned/hired/combat bots tick faster
        // than the TTL and keep the old behaviour.
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;
    };
}
