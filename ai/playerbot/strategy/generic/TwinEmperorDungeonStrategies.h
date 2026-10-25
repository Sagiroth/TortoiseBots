#pragma once
#include "playerbot/strategy/actions/TwinsOpeningActions.h"
namespace ai
{
class TwinPhysicalDpsStrategy : public TwinsOpeningStrategy
{
public:
    TwinPhysicalDpsStrategy(PlayerbotAI* ai) : TwinsOpeningStrategy(ai,"twins physical dps") {}
};
}
