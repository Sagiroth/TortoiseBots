#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class ArlokkPantherStrategy : public Strategy
    {
    public:
        ArlokkPantherStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "arlokk panther focus"; }
    };
    class HakkarPoisonStrategy : public Strategy
    {
    public:
        HakkarPoisonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "hakkar poison"; }
    };
    class JindoCurseStrategy : public Strategy
    {
    public:
        JindoCurseStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "jindo curse"; }
    };
    class ThekalBalanceStrategy : public Strategy
    {
    public:
        ThekalBalanceStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "thekal balance"; }
    };
    class MarliSpawnStrategy : public Strategy
    {
    public:
        MarliSpawnStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "marli spawn focus"; }
    };
}
