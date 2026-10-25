#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class ViscidusGlobStrategy : public Strategy
    {
    public:
        ViscidusGlobStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "viscidus glob focus"; }
    };
}
