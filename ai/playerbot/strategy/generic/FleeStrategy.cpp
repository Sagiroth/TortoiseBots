
#include "playerbot/playerbot.h"
#include "FleeStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/LastMovementValue.h"

using namespace ai;

float FleeMultiplier::GetValue(Action* action)
{
    // Only the basic "flee" action is dampened; "runaway" / "flee to master" are untouched.
    if (!action || action->getName() != "flee")
        return 1.0f;

    // Never let a flee interrupt a cast that is already underway: finishing the spell takes
    // priority. (Same predicate MoveTo() uses before it calls InterruptSpell().)
    if (bot->IsNonMeleeSpellCasted(true))
        return 0.0f;

    // The subsequent-flee loop break only applies to ranged casters (mage/warlock/priest).
    // Melee bots and hunter-kite logic are left untouched.
    if (!ai->IsRanged(bot))
        return 1.0f;

    LastMovement& lastMovement = AI_VALUE(LastMovement&, "last movement");
    const time_t now = time(0);
    // Mirrors the flee timing scale in MovementAction::Flee() (fleeDelay = urand(2, window)).
    const time_t window = (time_t)(sPlayerbotAIConfig.returnDelay / 1000);

    // No recent flee within the window -> not a flee loop. Allow flee at full relevance so the
    // bot still makes a genuine attempt to gain distance (correct when the target is escapable
    // or about to be crowd-controlled).
    if (!lastMovement.lastFleeAttempt || (now - lastMovement.lastFleeAttempt) > window)
        return 1.0f;

    // Subsequent flee within the window: once the bot has already fled twice in a row with no
    // progress, suppress flee so the low-priority nukes (fireball/frostbolt) outrank it. The
    // bot then plants and casts instead of running forever; the cast, once started, is kept
    // alive by the IsNonMeleeSpellCasted guard above. Higher-priority reactions the bot is
    // programmed for (Frost Nova, Blast Wave, blink, ...) still win normally.
    if (lastMovement.fleeCount >= 2)
        return 0.01f;

    return 1.0f;
}

void FleeStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new FleeMultiplier(ai));
}

void FleeStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "panic",
        NextAction::array(0, new NextAction("flee", ACTION_EMERGENCY + 9), NULL)));

    triggers.push_back(new TriggerNode(
        "outnumbered",
        NextAction::array(0, new NextAction("flee", ACTION_EMERGENCY + 9), NULL)));
}

void FleeFromAddsStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "has nearest adds",
        NextAction::array(0, new NextAction("runaway", 50.0f), NULL)));
}
