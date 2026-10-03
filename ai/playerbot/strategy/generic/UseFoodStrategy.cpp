
#include "playerbot/playerbot.h"
#include "UseFoodStrategy.h"

using namespace ai;

void UseFoodStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // A bot whose food and drink are free (the item cheat hands it conjured
    // rations) rests to AlmostFullHealth, not MediumHealth - the donor only
    // rests to medium (mod-playerbots' UseFoodStrategy registers
    // "medium health" -> food with the food cheat), but a levelling pool
    // starts its next pull from there and loses the near-won fights: at
    // levels 6-9 roughly a third of all deaths end with the killer already
    // under half health, i.e. fights a fuller bar wins. The start threshold
    // is unchanged (the band still opens at critical/low/medium); only the
    // stop rises, via the "almost full health" band and ShouldEatValue.
    bool freeFood = ai->HasCheat(BotCheatMask::item);

    // "almost full health" is [MediumHealth, AlmostFullHealth) here (while
    // "medium health" is [LowHealth, MediumHealth) and "low health" is
    // [CriticalHealth, LowHealth)), so the four together cover everything
    // under AlmostFullHealth; the critical band has to be named too or
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
        triggers.push_back(new TriggerNode(
            "almost full health",
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
