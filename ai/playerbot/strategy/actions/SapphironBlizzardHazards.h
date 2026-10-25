#pragma once
#include "playerbot/strategy/values/HazardsValue.h"

namespace ai
{
    std::list<HazardPosition> GetSapphironBlizzardHazards(PlayerbotAI* ai);
    bool SapphironBlizzardTooClose(PlayerbotAI* ai);
}
