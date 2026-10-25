#pragma once
#include "playerbot/strategy/values/HazardsValue.h"

namespace ai
{
    std::list<HazardPosition> GetGehennasRainHazards(PlayerbotAI* ai);
    bool GehennasRainTooClose(PlayerbotAI* ai);
}
