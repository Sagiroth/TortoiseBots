#pragma once

// Attack order result (AttackAction::Attack): when is an attack order on a
// target already fulfilled? The core's Unit::Attack returns false for the
// victim the unit already attacks, unless it only has to switch on the melee
// swing. The action passed that false on as a failure, so a bot already
// hitting its target logged "melee - FAILED" every time it re-picked melee
// (seen live on a tank and a rogue in Uldaman). Only explicit orders (attack
// my target, RTI) treat it as done; the melee action goes USELESS instead,
// because a success ends the engine tick (tools/test_attack_order_wiring.py).
// Pure decision logic, unit-tested in tools/test_attack_order_policy.cpp.

namespace TortoiseBots
{

// victimIsTarget: the bot's current victim is the ordered target.
// wantMelee: the order arms the melee swing (AttackAction's meleeEval).
// meleeArmed: the bot's melee swing is already on.
inline bool IsAlreadyAttackingTarget(bool victimIsTarget, bool wantMelee, bool meleeArmed)
{
    if (!victimIsTarget)
        return false;
    // Same victim, but the swing still has to be switched on: Unit::Attack
    // does that and returns true, so let it run.
    return !wantMelee || meleeArmed;
}

} // namespace TortoiseBots
