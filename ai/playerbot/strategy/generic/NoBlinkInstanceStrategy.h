#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // Player option `.bot behavior <bot> noblink on`: the mage never casts Blink inside a
    // dungeon or raid. Marker only; the Blink actions check for it.
    class NoBlinkInstanceStrategy : public Strategy
    {
    public:
        NoBlinkInstanceStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "no blink instance"; }
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "no blink instance"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "Stops a mage bot from casting Blink inside dungeons and raids. Set by .bot behavior <bot> noblink on.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { }; }
#endif
    };
}
