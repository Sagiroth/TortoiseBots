#pragma once
#include <cstdint>

namespace ai
{
    inline bool VaelTankHandoverActive(bool optedIn, bool inBlackwingLair,
        bool botAlive, bool botInCombat, bool victimIsTank, bool victimHasBurningAdrenaline)
    {
        return optedIn && inBlackwingLair && botAlive && botInCombat &&
            victimIsTank && victimHasBurningAdrenaline;
    }

    inline bool VaelBlockAutomaticTaunt(bool botHasBurningAdrenaline,
        bool victimHasBurningAdrenaline, bool victimIsOtherHealthyTank)
    {
        return botHasBurningAdrenaline || victimHasBurningAdrenaline || victimIsOtherHealthyTank;
    }
}
