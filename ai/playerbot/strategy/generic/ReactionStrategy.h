#pragma once

namespace ai
{
    // The reaction engine's universal raid rows (ReactionRowsPolicy.h), gated by AiPlayerbot.RaidReactions. The four
    // state rows (combat start/end, death, resurrect) are not here: ClassStrategy::InitReactionTriggers keeps them.
    class ReactionStrategy : public Strategy
    {
    public:
        ReactionStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        virtual int GetType() override { return STRATEGY_TYPE_REACTION; }

    protected:
        virtual void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    };
}
