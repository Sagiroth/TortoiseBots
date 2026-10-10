
#include "playerbot/playerbot.h"
#include "SpreadDistanceAction.h"

using namespace ai;

bool SpreadDistanceAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string param = event.GetParam();
    if (param == "?" || param.empty())
    {
        float curVal = AI_VALUE(float, "spread distance");
        std::ostringstream out;
        out << "spread distance: ";
        if (curVal > 0.0f)
            out << curVal << " yd";
        else
            out << (ai->IsRanged(bot) ? "5 yd (ranged default)" : "2 yd (melee default)");
        ai->TellPlayer(requester, out.str());
        return true;
    }
    if (param == "off" || param == "reset" || param == "disable")
    {
        RESET_AI_VALUE(float, "spread distance");
        ai->TellPlayer(requester, "spread distance reset to role default");
        return true;
    }
    float newVal = (float)atof(param.c_str());
    if (!(newVal > 0.0f))
        return false;
    SET_AI_VALUE(float, "spread distance", newVal);
    std::ostringstream out;
    out << "spread distance set to: " << newVal << " yd";
    ai->TellPlayer(requester, out.str());
    return true;
}
