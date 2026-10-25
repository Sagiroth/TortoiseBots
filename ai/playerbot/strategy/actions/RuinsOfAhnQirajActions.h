#pragma once
#include "playerbot/strategy/Action.h"
namespace ai
{
class OssirianCrystalAction : public Action
{
public:
    OssirianCrystalAction(PlayerbotAI* ai) : Action(ai,"ossirian crystal click") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
}
