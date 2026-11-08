#pragma once

// Pure gate for Power Word: Shield on a party member. A shield eats the hits
// a rage tank lives on (absorbed hits give no rage), so on a tank that uses
// rage the shield is kept for the save: only at or below the critical band,
// whether or not the tank is in combat. A 30 s shield cast between pulls still
// absorbs the next pull's opening hits. Everyone else shields as usual, so the
// critical rows keep the shield while the other band rows fall through to
// direct heals.
//
// Callers pass game facts (tank role from PlayerbotAI::IsTank, rage from
// GetPowerType() == POWER_RAGE, health percent, config critical health); the
// rule stays testable in tools/test_shield_target_policy.cpp.

namespace ai
{
    // A levelling warrior tanking in Arms/Fury or a druid tanking without
    // Feral talents reads as DPS to the talent-role check, but the stance
    // or form says tank all the same. Count either as a tank for the shield
    // gate so the priest does not eat the rage they are tanking on. An
    // explicitly assigned non-tank role wins over the stance: an ordered
    // DPS in Defensive Stance or Bear Form is still a DPS.
    inline bool TankForShield(bool roleTank, bool explicitNonTankRole, bool warriorInDefensiveStance, bool druidInBearForm)
    {
        return roleTank || (!explicitNonTankRole && (warriorInDefensiveStance || druidInBearForm));
    }

    // A shield eats the hits a rage tank lives on; keep it for the save on a
    // rage tank whether or not it is in combat, because a 30 s shield cast
    // between pulls still absorbs the next pull's opening hits. Callers pass
    // tank role, rage, health percent and the config critical health.
    inline bool ShouldShieldTarget(bool targetIsTank, bool targetUsesRage,
                                   unsigned targetHealthPct, unsigned criticalHealthPct)
    {
        if (targetIsTank && targetUsesRage)
            return targetHealthPct <= criticalHealthPct;
        return true;
    }
}
