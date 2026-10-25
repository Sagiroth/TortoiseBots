#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class YaujFearStrategy : public Strategy
    {
    public:
        explicit YaujFearStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "yauj fear"; }
    };
}
