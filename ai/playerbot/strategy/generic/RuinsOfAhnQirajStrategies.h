#pragma once
#include "playerbot/strategy/Strategy.h"
namespace ai
{
class OssirianCrystalStrategy : public Strategy
{
public:
    OssirianCrystalStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "ossirian crystal"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override
    {
        triggers.push_back(new TriggerNode("ossirian crystal ready",
            NextAction::array(0,new NextAction("ossirian crystal click",80.0f),nullptr)));
    }
};
}
