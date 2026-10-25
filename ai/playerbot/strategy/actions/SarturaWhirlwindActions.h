#pragma once
#include "DungeonActions.h"

namespace ai
{
    std::list<HazardPosition> GetSarturaWhirlwindHazards(PlayerbotAI* ai);
    bool SarturaWhirlwindTooClose(PlayerbotAI* ai);

    class SarturaWhirlwindEscapeAction : public MoveAwayFromHazard
    {
    public:
        explicit SarturaWhirlwindEscapeAction(PlayerbotAI* ai) :
            MoveAwayFromHazard(ai, "sartura whirlwind escape") {}
        bool Execute(Event& event) override;
    };
}
