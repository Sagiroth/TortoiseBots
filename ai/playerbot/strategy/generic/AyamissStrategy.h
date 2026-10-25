#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class AyamissLarvaStrategy : public Strategy
    {
    public:
        explicit AyamissLarvaStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "ayamiss larva focus"; }
    };
}
