#include "playerbot/playerbot.h"
#include "BossTacticsStrategy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"

using namespace ai;

void BossTacticsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // An interrupt goes first: a Chain Lightning that lands costs more than a
    // target switch. The kill-first pick sits above the generic assist rows so
    // the add becomes the target before the assist re-pick runs.
    triggers.push_back(new TriggerNode(
        "boss tactics interrupt",
        NextAction::array(0, new NextAction("boss tactics interrupt", ACTION_INTERRUPT + 1), NULL)));

    // A protected aura applied after an area cure started: the cure's next tick or pulse would remove it, so the
    // bot-cast Abolish aura is removed and the bot's own cleansing totem destroyed before that.
    triggers.push_back(new TriggerNode(
        "boss tactics dispel sweep",
        NextAction::array(0, new NextAction("boss tactics dispel sweep", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "boss tactics kill first",
        NextAction::array(0, new NextAction("boss tactics kill first", ACTION_HIGH + 1), NULL)));

    // A DPS bot on a boss entry a hold_kill row holds leaves it (the blocker is the kill): below the kill-first pick,
    // which is where it switches to first, and above the generic assist rows the multiplier stands down meanwhile.
    triggers.push_back(new TriggerNode(
        "boss tactics hold kill",
        NextAction::array(0, new NextAction("boss tactics hold kill", ACTION_HIGH + 2), NULL)));

    // A linked_death group is held: a DPS bot on another member takes the equalise pick, so the raid spreads its damage
    // instead of leaving one member to die and be raised.
    triggers.push_back(new TriggerNode(
        "boss tactics linked death",
        NextAction::array(0, new NextAction("boss tactics linked death", ACTION_HIGH + 3), NULL)));

    // A dps_hold_after row holds its subject (the pull, a band opening, a boss cast or gain, a blink): a free DPS bot
    // on it stops and waits the hold out. Above the kill-first pick, so the stop wins while the hold runs; the
    // kill-first add itself stays allowed (the action re-picks it).
    triggers.push_back(new TriggerNode(
        "boss tactics dps hold",
        NextAction::array(0, new NextAction("boss tactics dps hold", ACTION_HIGH + 4), NULL)));

    // A freeze_on_aura row holds this bot (Mandokir's Gaze): it stands still and does nothing, above the walk-outs.
    triggers.push_back(new TriggerNode(
        "boss tactics freeze",
        NextAction::array(0, new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7), NULL)));

    // A flee_on_aura row sends this carrier out of the group (or to the tank), level with the avoid_cast walk-out.
    triggers.push_back(new TriggerNode(
        "boss tactics flee",
        NextAction::array(0, new NextAction("boss tactics flee", ACTION_EMERGENCY + 6), NULL)));

    // A use_object row designates one bot to click its game object: a click is as time-critical as an interrupt (the
    // weakness lands 5 s after it), so it sits above the target picks and below freeze, flee and avoid_cast. The same
    // action walks the clicker in from the row's lead.
    triggers.push_back(new TriggerNode(
        "boss tactics use object",
        NextAction::array(0, new NextAction("boss tactics use object", ACTION_INTERRUPT + 2), NULL)));

    // Mode tank_anchor: the tank the boss is on walks it to the object. Above the tank's reach and face rows, below
    // every EMERGENCY row, so a survival move still wins.
    triggers.push_back(new TriggerNode(
        "boss tactics object anchor",
        NextAction::array(0, new NextAction("boss tactics object anchor", ACTION_HIGH + 5), NULL)));

    // taunt_after_cast (mode until_tank, Hakkar's Cause Insanity): the row's off-tank takes the boss back once its
    // victim is no tank. Above the target picks, below the survival rows: a taunt is worth nothing to a dead tank.
    triggers.push_back(new TriggerNode(
        "boss tactics taunt after",
        NextAction::array(0, new NextAction("boss tactics taunt after", ACTION_HIGH + 7), NULL)));

    // A listed blast is being cast and this bot stands in it: the walk out comes first, above the generic step out of
    // a ground effect (avoid aoe, ACTION_EMERGENCY + 5), since the blast kills in one hit.
    triggers.push_back(new TriggerNode(
        "boss tactics avoid cast",
        NextAction::array(0, new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6), NULL)));
    // A seek_aura row has a cloud to stand in or a gather to reach: the walk in wins over the target picks and the
    // stay walk, and only the freeze and the two walk-outs outrank it. It runs from the reaction engine too (a held
    // bot, a bot in a pull's join window), because the poison's cloud lives 3 s.
    triggers.push_back(new TriggerNode(
        "boss tactics seek",
        NextAction::array(0, new NextAction("boss tactics seek", ACTION_EMERGENCY + 5), NULL)));

    // The second off-tank's source: take it back, pull one, drag it to the spot. Above the tank's reach and face rows.
    triggers.push_back(new TriggerNode(
        "boss tactics seek tank",
        NextAction::array(0, new NextAction("boss tactics seek tank", ACTION_HIGH + 6), NULL)));

    // A free DPS bot's source: the pre-damage down to the hold percent, then the burn once the gather is ready (the
    // kill_first slot's priority; the two never both apply on one boss).
    triggers.push_back(new TriggerNode(
        "boss tactics seek kill",
        NextAction::array(0, new NextAction("boss tactics seek kill", ACTION_HIGH + 2), NULL)));

    // A stand set applies (ai_boss_stand): the walk to the seat comes before every cast and heal, the way the
    // reactive Deep Breath dodge does.
    triggers.push_back(new TriggerNode(
        "boss tactics stand",
        NextAction::array(0, new NextAction("boss tactics stand", ACTION_EMERGENCY), NULL)));

    triggers.push_back(new TriggerNode(
        "boss tactics front",
        NextAction::array(0, new NextAction("boss tactics step behind", ACTION_HIGH), NULL)));

    // Leaving the map ends the strategy even from inside a fight.
    triggers.push_back(new TriggerNode(
        "leave boss tactics",
        NextAction::array(0, new NextAction("disable boss tactics strategy", 100.0f), NULL)));
}

void BossTacticsStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // A frozen carrier does nothing out of combat either: a heal, a buff or a walk could start there.
    triggers.push_back(new TriggerNode(
        "boss tactics freeze",
        NextAction::array(0, new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7), NULL)));

    // A held bot out of combat itself, beside a group attacker's blast, runs the non-combat engine.
    triggers.push_back(new TriggerNode(
        "boss tactics avoid cast",
        NextAction::array(0, new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "leave boss tactics",
        NextAction::array(0, new NextAction("disable boss tactics strategy", 100.0f), NULL)));
}

// The combat engine sleeps through the bot's own cast (PlayerbotAI::UpdateAI), and a heal or a bolt outlasts the
// warning of a 3 s blast. The reaction engine runs every update; the walk-out breaks the bot's cast itself once a walk
// is under way (BossTacticsAvoidCastAction).
void BossTacticsStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // The freeze stops the bot's own walk and cast the tick the aura lands, before the reaction engine's own
    // break-and-stop can restart anything.
    triggers.push_back(new TriggerNode(
        "boss tactics freeze",
        NextAction::array(0, new NextAction("boss tactics freeze", ACTION_EMERGENCY + 7), NULL)));

    // The flee row's carrier leaves the group from the reaction engine too, like the hard-coded bomb runout it can
    // replace (ReactionStrategy.cpp).
    triggers.push_back(new TriggerNode(
        "boss tactics flee",
        NextAction::array(0, new NextAction("boss tactics flee", ACTION_EMERGENCY + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "boss tactics avoid cast",
        NextAction::array(0, new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6), NULL)));

    // The walk into the poison, from the reaction engine as well: a bot held by a stay, by "wait for attack" or by
    // its own cast still has to reach the cloud the source left.
    triggers.push_back(new TriggerNode(
        "boss tactics seek",
        NextAction::array(0, new NextAction("boss tactics seek", ACTION_EMERGENCY + 5), NULL)));
}

void BossTacticsStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new BossTacticsFreezeMultiplier(ai));
    multipliers.push_back(new BossTacticsHoldMultiplier(ai));
    multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));
    multipliers.push_back(new BossTacticsDispelHoldMultiplier(ai));
    multipliers.push_back(new BossTacticsSeekHoldMultiplier(ai));
}

// The hold on the non-combat engine too: a held bot out of combat itself would walk back to its stay spot in the
// blast it just left. (The trap hold is combat-only - outside a fight the follow and the return to the stay are the
// engine's travel, not a walk back into fire - and it hangs on the always-on combat strategies instead, so it does
// not need boss tactics to be on.)
void BossTacticsStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new BossTacticsFreezeMultiplier(ai));
    multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));
}

// The reaction engine runs every update, so a frozen bot's buff, heal or walk has to be held there as well.
void BossTacticsStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new BossTacticsFreezeMultiplier(ai));
}
