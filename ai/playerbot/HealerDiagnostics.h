#pragma once
#include "HealerDiagnosticPolicy.h"
#include <cstdint>

class PlayerbotAI;
class Unit;

namespace ai
{
class HealerDiagnostics
{
public:
    void CheckFailed(PlayerbotAI* ai, Unit* target, std::uint32_t spellId, std::uint32_t result);
    void CastAttempt(PlayerbotAI* ai, Unit* target, std::uint32_t spellId, bool accepted);
    void Sample(PlayerbotAI* ai);
private:
    HealerDiagnosticClock samples;
    HealerDiagnosticClock skips;
    std::uint32_t failedChecks = 0;
    std::uint32_t castAttempts = 0;
    std::uint32_t acceptedAttempts = 0;
};
}
