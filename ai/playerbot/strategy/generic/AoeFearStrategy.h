#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // Marker strategy (issue #383 follow-up): AoE fears (Psychic Scream,
    // Howl of Terror, Intimidating Shout) are off by default inside
    // dungeons/raids and with a real player master, because a feared mob runs
    // into the next pack. A player can opt a bot back in with "co +aoe fear"
    // (and out again with "co -aoe fear"). The strategy adds no triggers; the
    // fear actions read it through AoeFearAllowed.
    class AoeFearStrategy : public Strategy
    {
    public:
        AoeFearStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        virtual int GetType() override { return STRATEGY_TYPE_COMBAT; }
        virtual std::string getName() override { return "aoe fear"; }
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "aoe fear"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "Allows the bot to use AoE fears (Psychic Scream, Howl of Terror, Intimidating Shout) inside dungeons and raids and while grouped with a player, where they are off by default.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { "cc" }; }
#endif
    };
}
