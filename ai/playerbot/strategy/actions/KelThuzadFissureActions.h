#pragma once
#include "DungeonActions.h"
namespace ai
{
    std::list<HazardPosition> GetKelThuzadFissures(PlayerbotAI* ai);
    bool InsideKelThuzadFissure(PlayerbotAI* ai);
    class KelThuzadFissureEscapeAction : public MovementAction
    {
    public:
        KelThuzadFissureEscapeAction(PlayerbotAI* ai) : MovementAction(ai, "kt fissure escape") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
}
