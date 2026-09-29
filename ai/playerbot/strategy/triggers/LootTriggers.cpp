
#include "playerbot/playerbot.h"
#include "LootTriggers.h"
#include "playerbot/LootObjectStack.h"
using namespace ai;

bool LootAvailableTrigger::IsActive()
{
    // The loot chain only exists in the non-combat engine, and that engine's state comes from the
    // "combat start"/"combat end" reaction (has attackers) - not from UNIT_FLAG_IN_COMBAT, which
    // lingers for a moment after every kill. Testing the flag here suppressed "loot" (6.0) for
    // exactly the window in which the bot must loot before picking its next target, so
    // "attack anything" (5.0, GrindingStrategy) ordered the next pull instead: live measurement
    // showed that a kill followed by an attack order within 2s produced loot on 7% of corpses,
    // against 43% when the bot stayed quiet for 5-10s. The corpse then expired unopened.
    // "combat" also counted any group member in combat within reactDistance (150y), so a bot
    // party stopped looting while anyone nearby fought.
    //
    // bot->GetAttackers() replaces it with the core's own "attacking me right now" set, which has
    // neither of those two defects: it is filled in Unit::Attack (the moment an add aggros, before
    // its first hit) and drained in CombatStop/AttackStop (the moment it dies or evades). That
    // closes the engine's blind spot - "has attackers" is a 2s-cached list, so a corpse could make
    // the bot kneel for lootDelay next to an add that already had it as its victim - while a kill
    // still loots at once (the dead mob is gone from the set) and a corpse held up by a real fight
    // waits in the stack for LOOT_OBJECT_TTL_SECONDS instead of being lost.
    if (AI_VALUE2(bool, "mounted", "self target") || !bot->GetAttackers().empty())
        return false;

    if (!AI_VALUE(bool, "has available loot"))
        return false;

    // Loot every corpse this bot (or its group) killed and may loot before
    // pulling the next mob, like a player. Fire the selection action ("loot")
    // when no corpse is picked yet, or the picked one can no longer be looted
    // or reached (looted by someone else, despawned, expired, or abandoned
    // after a failed approach): "loot" then picks the nearest corpse still on
    // the stack, so the chain repairs itself instead of stalling.
    //
    // The old "already within INTERACTION_DISTANCE, or no hostile targets
    // around" gate starved the chain in mob-dense grind zones: "attack
    // anything" (GrindingStrategy, 5.0) grabbed the next mob and the bot walked
    // off, and a selected corpse that went stale was never replaced, so the
    // "far from current loot" -> "move to loot" path (7.0) could not run.
    // While a valid target is selected, those two actions walk to and open it,
    // so re-selecting the target every tick is neither needed nor desirable.
    LootObject lootTarget = AI_VALUE(LootObject, "loot target");
    return lootTarget.IsEmpty() || !lootTarget.IsLootPossible(bot);
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");

    if (!loot.IsLootPossible(bot))
        return false;

    // Abandon loot the bot cannot safely reach without breaking its follow leash.
    // "move to loot" runs at priority 7 but "out of free move range" fires "follow" at
    // ACTION_HIGH (20). A corpse is only reachable without oscillation if the master is
    // within followDistance + GetMaxLootDistance of it — otherwise the bot must leave the
    // leash to reach the corpse and follow immediately wins, causing a yo-yo. Same predicate
    // the loot stack uses to decide whether the corpse counts as available loot at all.
    if (!AI_VALUE(LootObjectStack*, "available loot")->IsWithinMasterLootRange(loot))
        return false;

    // Must agree with OpenLootAction::DoLoot's loot-range rule, or the bot deadlocks: while the
    // 2D "distance" value here read "close enough", "open loot" (8.0) outranked "move to loot"
    // (7.0) and failed the server's 3D range check every tick, so on sloped ground the bot sat
    // a few yards off a corpse it could not loot and never approached it.
    return !loot.IsInLootRange(bot);
}

bool CanLootTrigger::IsActive()
{
    return AI_VALUE(bool, "can loot");
}
