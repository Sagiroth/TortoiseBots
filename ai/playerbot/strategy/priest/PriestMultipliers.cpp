
#include "playerbot/playerbot.h"
#include "PriestMultipliers.h"
#include "PriestActions.h"

using namespace ai;

namespace
{
    bool BotKnowsHealRank2Plus(Player* bot)
    {
        return bot->HasSpell(2055) || bot->HasSpell(6063) || bot->HasSpell(6064);
    }
}

bool ai::PriestShouldSuppressLesserHeal(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot || !BotKnowsHealRank2Plus(bot))
        return false;
    if (!ai->CanCastSpell("heal", bot))
        return false;
    return PriestSuppressLesserHeal(true, true);
}

float PriestLesserHealMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    std::string const& actionName = action->getName();
    if (actionName != "lesser heal" && actionName != "lesser heal on party")
        return 1.0f;
    return PriestShouldSuppressLesserHeal(ai) ? 0.0f : 1.0f;
}
