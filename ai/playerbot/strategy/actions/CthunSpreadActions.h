#pragma once
#include "MovementActions.h"
#include <map>
#include "playerbot/CthunSpreadPolicy.h"

namespace ai
{
    bool CthunSpreadEnabled(PlayerbotAI* ai);
    bool SelectCthunSpreadTarget(PlayerbotAI* ai, Unit*& target);
    class CthunSpreadAction : public MovementAction
    {
    public:
        explicit CthunSpreadAction(PlayerbotAI* ai) : MovementAction(ai, "cthun spread move") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
        bool Assigned(CthunPoint& point);
        bool CanHoldPosition() const { return !routeBlocked; }
        std::vector<CthunPlacement> const& GroupPlan();
    private:
        bool Prepare();
        bool routeBlocked = false;
        CthunPoint lastDestination, preparedDestination;
        uint32 lastSpline = 0;
        std::map<uint32,unsigned> roles;
        std::vector<WorldPosition> preparedPath;
        uint32 lastPlan = 0;
        uint32 lastMove = 0;
        uint32 lastCheck = 0;
        bool preparedUseful = false;
        uint32 lastReport = 0;
        std::vector<CthunPlacement> plan;
    };
}
