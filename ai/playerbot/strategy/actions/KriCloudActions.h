#pragma once
#include "DungeonActions.h"

namespace ai
{
    std::list<HazardPosition> GetKriCloudHazards(PlayerbotAI* ai);
    bool KriCloudTooClose(PlayerbotAI* ai);

    class KriCloudEscapeAction : public MoveAwayFromHazard
    {
    public:
        explicit KriCloudEscapeAction(PlayerbotAI* ai) :
            MoveAwayFromHazard(ai, "kri cloud escape") {}
        bool Execute(Event& event) override;
    };
}
