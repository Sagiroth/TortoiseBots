#pragma once
#include <string>

#ifndef GenerateBotHelp
//#define GenerateBotHelp //Enable only for help generation
#endif

#ifndef GenerateBotTests
//#define GenerateBotTests //Enable only for test generation
#endif

class PlayerbotAI;

namespace ai
{
    class PlayerbotAIAware
    {
    public:
        PlayerbotAIAware(PlayerbotAI* const ai) : ai(ai), botAI(ai) { }
        virtual ~PlayerbotAIAware() = default;
        virtual std::string getName() { return std::string(); }
    protected:
        PlayerbotAI* ai;
        // Forward-ported class strategies use the donor's botAI spelling.
        // Keep it as a module-local alias while legacy Tortoise code uses ai.
        PlayerbotAI* botAI;
    };
}
