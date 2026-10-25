#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class HakkarPoisonStrategy : public Strategy
    {
    public:
        HakkarPoisonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "hakkar poison"; }
    };
}
