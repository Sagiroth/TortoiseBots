#pragma once

// MainTankPolicy — pure main-tank rules (LD-8, mod-playerbots parity).
//
// Donor behaviour (mod-playerbots, read-only reference): the raid/party has
// one designated main tank (`PlayerbotAI::GetMainTankGuid`: the explicit raid
// main-tank flag, else the first live tank); `LowTankThreatTrigger` fires when
// the bot climbs past half the main tank's threat (or the tank holds nothing);
// and the explicit main tank sticks to its current target when several tanks
// share the group (`TankTargetValue.cpp:76-83`).
//
// Vanilla notes: Misdirection / Tricks of the Trade (the donor trigger's only
// consumers) do not exist in 1.18.1, so the trigger ships registered but
// unwired — raid tactics consume it later. The raid main-tank flag exists in
// our core (`Group::GetMainTankGuid`), honouring it is free.

namespace ai
{
    // Donor `LowTankThreatTrigger::IsActive` (GenericTriggers.cpp:177-191):
    // players hold no threat table, so a player target never fires; a tank
    // with zero threat means the pull is uncontrolled and always fires.
    inline bool LowTankThreatFires(bool targetIsPlayer, float botThreat, float tankThreat)
    {
        if (targetIsPlayer)
            return false;

        if (tankThreat == 0.0f)
            return true;

        return botThreat > tankThreat * 0.5f;
    }

    // Deferred-spec anchor (review PR #573): no call sites yet — nothing in
    // TankTargetValue.cpp includes or calls this. Pins the donor rule for a
    // future explicit-only, stickiness-first split; tank picks are unchanged.
    // Donor explicit-MT stickiness (TankTargetValue.cpp:76-83): only the
    // explicitly flagged main tank, and only when the group fields more than
    // one tank — a lone tank keeps the normal loose-add tournament.
    inline bool MainTankSticksToCurrent(bool isExplicitMainTank, unsigned groupTankCount)
    {
        return isExplicitMainTank && groupTankCount > 1;
    }
}
