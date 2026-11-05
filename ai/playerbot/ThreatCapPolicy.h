#pragma once

// The threat reference a damage dealer's threat percentage is measured against (ThreatValue::GetTankThreat): the
// highest tank's threat. On a boss whose tactic data asks for a successor (ai_boss_tactic kind `threat_successor`,
// Hakkar) the second tank has to stay second so the boss turns to it on a threat reset: there the reference is the
// highest threat among the assigned off-tanks on the target, as soon as that is above a tiny absolute floor. The
// trigger is data, never the creature's taunt immunity: most raid bosses in this world data are taunt-immune and must
// keep the highest tank, or a zero reference would read as "the tank has no threat" and veto every damage action.
// Tested in tools/test_threat_cap_policy.cpp; the caller resolves the raid's tank order into plain numbers, so this
// header stays free of core includes.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ai
{
    // One tank on the target as the caller resolved it from the raid's tank order (GetRaidTankRoles): index 0 is the
    // main tank, every later one an assigned off-tank.
    struct ThreatCapTank
    {
        float threat = 0.0f;
        bool offTank = false;
    };

    // The off-tank floor, as an absolute amount rather than a share of the highest tank's threat: the reference is
    // whichever of a 1 % share or this fixed amount is larger. A share-only floor (the 50 % this policy once used) sat
    // the cap at the main tank whenever the off-tank dealt a fraction of the main tank's damage, and Hakkar's Cause
    // Insanity then deleted exactly that tank's threat and handed him to the top damage dealer (walk 41: 43 % of the
    // boss's post-charm victims were not tanks). The floor only keeps a barely-engaged or reset off-tank (dead, or its
    // threat just deleted) from capping the damage dealers at almost nothing.
    constexpr float kThreatCapOffTankMinShare = 0.01f;
    constexpr float kThreatCapOffTankMinThreat = 100.0f;

    // The reference and which kind of tank it is: `offTank` is the `threat_successor` cap standing on an assigned
    // off-tank, false when the highest tank is the reference.
    struct ThreatCapPick
    {
        float threat = -1.0f;
        bool offTank = false;
    };

    // -1 when no tank is on the target (the caller's own "no reference" answer, kept from the previous max scan).
    // Without the successor cap: the highest tank, as always. With it: the highest threat among the off-tanks that are
    // on the target (threat > 0) and stand above the absolute floor (kThreatCapOffTankMinThreat, or 1 % of the highest
    // tank when that is larger), and the highest tank when no off-tank does: a zero reference is never returned while
    // a tank stands, since the caller reads that as "the tank has no threat" and stops all damage.
    inline ThreatCapPick ThreatCapPickReference(const std::vector<ThreatCapTank>& tanks, bool successorCap)
    {
        ThreatCapPick pick;
        if (tanks.empty())
            return pick;
        float highest = 0.0f;
        for (const ThreatCapTank& tank : tanks)
            highest = std::max(highest, tank.threat);
        pick.threat = highest;
        if (!successorCap)
            return pick;
        bool haveOffTank = false;
        float offTank = 0.0f;
        for (const ThreatCapTank& tank : tanks)
        {
            if (!tank.offTank || tank.threat <= 0.0f)
                continue;
            offTank = haveOffTank ? std::max(offTank, tank.threat) : tank.threat;
            haveOffTank = true;
        }
        const float floor = std::max(highest * kThreatCapOffTankMinShare, kThreatCapOffTankMinThreat);
        if (!haveOffTank || offTank <= floor)
            return pick;
        pick.threat = offTank;
        pick.offTank = true;
        return pick;
    }

    inline float ThreatCapReference(const std::vector<ThreatCapTank>& tanks, bool successorCap)
    {
        return ThreatCapPickReference(tanks, successorCap).threat;
    }
}
