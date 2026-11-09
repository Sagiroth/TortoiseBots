
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "CombatStrategy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "../../../../runtime/CommandedGroupQuery.h"

using namespace ai;

void CombatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "invalid target",
        NextAction::array(0, new NextAction("select new target", 89.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mounted",
        NextAction::array(0, new NextAction("check mount state", 88.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "combat stuck",
        NextAction::array(0, new NextAction("unstuck", 0.7f), NULL)));

    triggers.push_back(new TriggerNode(
        "combat long stuck",
        NextAction::array(0, new NextAction("unstuck", 0.9f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("use trinket", 50.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("use lightwell", 80.0f), NULL)));
}

float AvoidAoeStrategyMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    std::string name = action->getName();
    if (name == "follow" || name == "co" || name == "nc" || name == "react" || name == "select new target" || name == "flee")
        return 1.0f;

    uint32 spellId = AI_VALUE2(uint32, "spell id", name);
    const SpellEntry* const pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!pSpellInfo) return 1.0f;

    if (spellId && pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
        return 1.0f;
    else if (spellId && pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
        return 1.0f;

    uint32 CastingTime = !IsChanneledSpell(pSpellInfo) ? GetSpellCastTime(pSpellInfo, bot) : GetSpellDuration(pSpellInfo);

    if (AI_VALUE2(bool, "has area debuff", "self target") && spellId && CastingTime > 0)
    {
        return 0.0f;
    }

    return 1.0f;
}

void AvoidAoeStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Proactive strafe-to-safety: "aoe threat nearby" fires on any of the
    // three sensors (dynobj aura, damaging trap, trigger NPC — none of
    // which the old "has area debuff" row could see), so the bot steps out
    // BEFORE the debuff lands. The strafe self-gates via isUseful (bot
    // actually inside the hazard radius). Flee is deliberately NOT listed
    // here: the sensors scan 15-30yd while radii are often 5-8yd, so mere
    // proximity would full-flee, and a useful-but-unlandable strafe would
    // win the reaction pick every tick and starve the fallback.
    triggers.push_back(new TriggerNode(
        "aoe threat nearby",
        NextAction::array(0, new NextAction("avoid aoe", ACTION_EMERGENCY + 5), NULL)));
    // Reactive fallback: once the debuff actually lands, run out. Below the
    // strafe above, so a usable step-out wins the pick; a tank skips it in
    // FleeFromAreaAction::isUseful (fleeing a live boss drops threat and drags the
    // fight onto the casters).
    triggers.push_back(new TriggerNode(
        "has area debuff",
        NextAction::array(0, new NextAction("flee from area", ACTION_EMERGENCY + 4), NULL)));
}

void AvoidAoeStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);

    // A taunt-immune boss on a non-tank (AiPlayerbot.DungeonRunImmuneBossVictimEscape), from the reaction engine too:
    // the combat engine sleeps through the bot's own cast, and the boss's 2-4 k swings kill cloth inside one, so the
    // escape breaks that cast (ShouldReactionInterruptCast). Every bot's reaction engine carries this strategy; the
    // combat-engine copy is on the always-on dungeon strategy.
    triggers.push_back(new TriggerNode(
        "immune boss victim",
        NextAction::array(0, new NextAction("immune boss victim escape", ACTION_EMERGENCY + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "immune boss victim protect",
        NextAction::array(0, new NextAction("bop immune boss victim", ACTION_EMERGENCY + 2), NULL)));
}

void AvoidAoeStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new AvoidAoeStrategyMultiplier(ai));
    // The trap hold belongs to AoE avoidance, not to the boss tactics that first carried it: every bot
    // has this strategy, so the hold is not gated behind an encounter. Multipliers are per engine, so
    // the combat engine - which runs the reach, the follow and the stay - carries its own copy on the
    // always-on dungeon strategy; no engine ever gets both.
    multipliers.push_back(new TrapHoldMultiplier(ai));
    // The charmed ally's area veto (AiPlayerbot.DungeonRunCharmYield) travels with the trap hold for the same reason:
    // every bot has this strategy, so one charmed member's blast is stood down without an encounter gate.
    multipliers.push_back(new CharmedAllyHoldMultiplier(ai));
}

void AvoidAoeStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

void WaitForAttackStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "wait for attack safe distance",
        NextAction::array(0, new NextAction("wait for attack keep safe distance", 60.0f), NULL)));
    // Ordered-pull hold release: each held bot drops its own anchor hold
    // once its join window elapses (see "pull hold expired"). High relevance
    // so the release wins over the drift guard while both are active.
    triggers.push_back(new TriggerNode(
        "pull hold expired",
        NextAction::array(0, new NextAction("release pull hold", ACTION_HIGH), NULL)));
}

void WaitForAttackStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "pull hold expired",
        NextAction::array(0, new NextAction("release pull hold", ACTION_HIGH), NULL)));
}

void WaitForAttackStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new WaitForAttackMultiplier(ai));
}

bool WaitForAttackStrategy::ShouldWait(PlayerbotAI* ai)
{
    // The run leader's healer is held on its stand for a pull but never kept
    // from healing: the join window gates damage, and it deals none.
    if (ai->RunHealOnly() && PlayerbotAI::IsHeal(ai->GetBot()))
        return false;
    // Patchwerk: DPS wait through the opening hold, assigned soakers join at once.
    if (PatchwerkDpsOpeningHold(ai))
        return true;
    if (GetPatchwerkSoakBoss(ai))
        return false;

    // Only check if the bot has the strategy enabled
    if (ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
    {
        // Only check if the bot is in a group with a real player, or in a party
        // bound to a dungeon run while a run-leader pull holds it (a hired run
        // party's master is its hirer, often a real player outside the group,
        // so the first clause is usually true there; the second covers a
        // masterless bound party). A bot's own stored "wait for attack" never
        // holds at every fight start in a bot-only run: without a pull window
        // there is no pull to wait for (lead decision on ta265).
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();
        if (bot->GetGroup() && (ai->HasRealPlayerMaster() ||
            (TortoiseBots::BotCommands::IsInRunningDungeonRun(bot) && ai->HasPullWindow())))
        {
            // Don't wait if the current target is an enemy player
            bool enemyPlayer = false;
            Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
            if (target)
            {
                Player* player = dynamic_cast<Player*>(target);
                if (player)
                {
                    enemyPlayer = !sServerFacade.IsFriendlyTo(target, player);
                }
            }

            if (!enemyPlayer)
            {
                // Check if bot is currently in combat
                const time_t combatStartTime = AI_VALUE(time_t, "combat start time");
                if (combatStartTime > 0)
                {
                    // Check the amount of time elapsed from the combat start
                    const time_t elapsedTime = time(0) - combatStartTime;
                    return elapsedTime < GetWaitTime(ai);
                }
            }
        }
    }

    return false;
}

uint8 WaitForAttackStrategy::GetWaitTime(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(uint8, "wait for attack time");
}

float WaitForAttackMultiplier::GetValue(Action* action)
{
    // Allow some movement and targeting actions
    const std::string& actionName = action->getName();
    if ((actionName != "wait for attack keep safe distance") &&
        (actionName != "dps assist") &&
        (actionName != "set facing") &&
        (actionName != "pull my target") &&
        (actionName != "pull rti target") &&
        (actionName != "pull start") &&
        (actionName != "pull action") &&
        (actionName != "pull end") &&
        (actionName != "release pull hold") &&
        (actionName != "boss tactics avoid cast"))
    {
        return WaitForAttackStrategy::ShouldWait(ai) ? 0.0f : 1.0f;
    }

    return 1.0f;
}

void HealInterruptStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "heal target full health",
        NextAction::array(0, new NextAction("interrupt current spell", ACTION_EMERGENCY), NULL)));
}

void HealInterruptStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void SpreadStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "spread needed",
        NextAction::array(0, new NextAction("raid spread", ACTION_NORMAL), NULL)));
}
