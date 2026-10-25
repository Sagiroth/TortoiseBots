#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/actions/RuinsOfAhnQirajActions.h"
namespace ai
{
class OssirianCrystalTrigger : public Trigger
{
public:
    OssirianCrystalTrigger(PlayerbotAI* ai) : Trigger(ai,"ossirian crystal ready",1) {}
    bool IsActive() override
    {
        OssirianCrystalAction action(ai);
        return action.isUseful();
    }
};
}
