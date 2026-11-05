#pragma once

// The threat reference a damage dealer's threat percentage is measured against (ThreatValue::GetTankThreat): the
// highest tank's threat. On a boss whose tactic data asks for a successor (ai_boss_tactic kind `threat_successor`,
// Hakkar) the second tank has to stay second so the boss turns to it on a threat reset: there the reference is the
// highest threat among the assigned off-tanks on the target. The trigger is data, never the creature's taunt immunity:
// most raid bosses in this world data are taunt-immune and must keep the highest tank, or a zero reference would read
// as "the tank has no threat" and veto every damage action. Tested in tools/test_threat_cap_policy.cpp; the caller
// resolves the raid's tank order into plain numbers, so this header stays free of core includes.

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

    // -1 when no tank is on the target (the caller's own "no reference" answer, kept from the previous max scan).
    // Without the successor cap: the highest tank, as always. With it: the highest threat among the off-tanks that are
    // on the target (threat > 0), and the highest tank when no off-tank is: a zero reference is never returned while a
    // tank stands, since the caller reads that as "the tank has no threat" and stops all damage.
    inline float ThreatCapReference(const std::vector<ThreatCapTank>& tanks, bool successorCap)
    {
        if (tanks.empty())
            return -1.0f;
        float highest = 0.0f;
        for (const ThreatCapTank& tank : tanks)
            highest = std::max(highest, tank.threat);
        if (!successorCap)
            return highest;
        bool haveOffTank = false;
        float offTank = 0.0f;
        for (const ThreatCapTank& tank : tanks)
        {
            if (!tank.offTank || tank.threat <= 0.0f)
                continue;
            offTank = haveOffTank ? std::max(offTank, tank.threat) : tank.threat;
            haveOffTank = true;
        }
        return haveOffTank ? offTank : highest;
    }
}
