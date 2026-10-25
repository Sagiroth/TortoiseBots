#pragma once
#include "DungeonActions.h"
#include "playerbot/SarturaDiagnosticPolicy.h"

namespace ai
{
    void LogSarturaEscape(PlayerbotAI* ai, char const* event, std::string const& reason,
        bool success = false, WorldPosition const* destination = nullptr);
    std::list<HazardPosition> GetSarturaWhirlwindHazards(PlayerbotAI* ai);
    bool SarturaWhirlwindTooClose(PlayerbotAI* ai);

    class SarturaWhirlwindEscapeAction : public MoveAwayFromHazard
    {
    public:
        explicit SarturaWhirlwindEscapeAction(PlayerbotAI* ai) :
            MoveAwayFromHazard(ai, "sartura whirlwind escape") {}
        bool Execute(Event& event) override;
        SarturaDiagnosticThrottle diagnosticThrottle;
    };
}
