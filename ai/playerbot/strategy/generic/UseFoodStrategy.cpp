
#include "playerbot/playerbot.h"
#include "UseFoodStrategy.h"

using namespace ai;

void UseFoodStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // A bot whose food and drink are free (the item cheat hands it conjured
    // rations) rests to MediumHealth, not LowHealth - the donor does exactly
    // this: mod-playerbots' UseFoodStrategy registers "medium health" -> food
    // when the bot has the food cheat, and "low health" only without it.
    // Resting to 50 % is why a levelling bot starts its next pull on half a
    // health bar: at levels 6-9 roughly a third of all deaths end with the
    // killer already under half health, i.e. fights a fuller bar wins.
    bool freeFood = ai->HasCheat(BotCheatMask::item);

    // "medium health" is [LowHealth, MediumHealth) and "low health" is
    // [CriticalHealth, LowHealth) in this module, so the two together cover
    // everything under MediumHealth; the critical band has to be named too or
    // a bot below LowHealth would have no food trigger at all. The engine
    // executes one action per tick, so naming the same action on several
    // triggers costs nothing.
    if (freeFood)
    {
        triggers.push_back(new TriggerNode(
            "critical health",
            NextAction::array(0, new NextAction("food", 6.0f), NULL)));
        triggers.push_back(new TriggerNode(
            "low health",
            NextAction::array(0, new NextAction("food", 6.0f), NULL)));
        triggers.push_back(new TriggerNode(
            "medium health",
            NextAction::array(0, new NextAction("food", 6.0f), NULL)));
    }
    else
    {
        triggers.push_back(new TriggerNode(
            "low health",
            NextAction::array(0, new NextAction("food", 6.0f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "high mana",
        NextAction::array(0, new NextAction("drink", 6.0f), NULL)));
}
