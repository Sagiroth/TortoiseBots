#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"

namespace ai
{

    class GrindTargetValue : public TargetValue
	{
	public:
        GrindTargetValue(PlayerbotAI* ai, std::string name = "grind target") : TargetValue(ai, name, 2) {}

    public:
        Unit* Calculate() override;

    private:
        int GetTargetingPlayerCount(Unit* unit);
        Unit* FindTargetForGrinding(int assistCount);
        // Idle-starter fallback: a wider live scan for the nearest in-cap
        // XP mob when the normal pick came back empty and the bot holds no
        // travel destination. Throttled per bot (see
        // GRIND_IDLE_FALLBACK_INTERVAL_MS) so a stranded pool does not pay
        // the wider grid visit every tick.
        Unit* FindIdleFallbackTarget();
        uint32 lastIdleFallbackMs = 0;
    };
}
