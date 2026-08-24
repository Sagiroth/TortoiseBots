#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/AiFactory.h"

namespace ai
{
    class BotRolesValue : public Uint8CalculatedValue, public Qualified
    {
    public:
        BotRolesValue(PlayerbotAI* ai, std::string name = "bot roles") : Uint8CalculatedValue(ai, name, 10), Qualified() {}
        virtual uint8 Calculate() override
        {
            return AiFactory::GetPlayerRoles(bot);
        }
    };
}
