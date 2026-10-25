#pragma once
#include "HazardsValue.h"
namespace ai
{
std::list<HazardPosition> CalculateGrobbulusCloudHazards(PlayerbotAI* ai);
class GrobbulusCloudHazardsValue : public CalculatedValue<std::list<HazardPosition>>
{
public:
    GrobbulusCloudHazardsValue(PlayerbotAI* ai) : CalculatedValue(ai,"grobbulus cloud hazards",1) {}
private:
    std::list<HazardPosition> Calculate() override { return CalculateGrobbulusCloudHazards(ai); }
};
std::list<HazardPosition> GetGrobbulusCloudHazards(PlayerbotAI* ai);
bool GrobbulusCloudMovementAllowed(PlayerbotAI* ai);
bool InsideGrobbulusCloud(PlayerbotAI* ai);
}
