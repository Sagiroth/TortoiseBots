#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class FankrissSpawnStrategy : public Strategy
    {
    public:
        explicit FankrissSpawnStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "fankriss spawn focus"; }
    };
}
