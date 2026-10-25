#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class SkeramImageStrategy : public Strategy
    {
    public:
        explicit SkeramImageStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "skeram image focus"; }
    };
}
