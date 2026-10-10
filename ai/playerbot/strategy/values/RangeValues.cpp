
#include "playerbot/playerbot.h"
#include "RangeValues.h"

using namespace ai;

RangeValue::RangeValue(PlayerbotAI* ai)
    : ManualSetValue<float>(ai, 0, "range"), Qualified()
{
}

std::string RangeValue::Save()
{
    std::ostringstream out; out << value; return out.str();
}

bool RangeValue::Load(std::string text)
{
    value = atof(text.c_str());
    return true;
}

SpreadDistanceValue::SpreadDistanceValue(PlayerbotAI* ai)
    : ManualSetValue<float>(ai, -1.0f, "spread distance"), Qualified()
{
}

std::string SpreadDistanceValue::Save()
{
    std::ostringstream out; out << value; return out.str();
}

bool SpreadDistanceValue::Load(std::string text)
{
    value = atof(text.c_str());
    return true;
}
