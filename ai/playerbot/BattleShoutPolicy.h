#pragma once

// Battle Shout vs Blessing of Might decision (warrior parity WAR-9).
//
// Pure rule, no engine types: given the bot's Battle Shout rank (as attack
// power) and the Blessing of Might aura on the bot (if any, as attack
// power), shouting is worthwhile only when no might is up or the shout is
// strictly stronger. Equal AP still skips: re-shouting over an equal buff
// wastes rage and a GCD for zero gain.
//
// AP values are spell_template EffectBasePoints+1 (verified 1.18.1 data):
// shout R1-R7 = 15/35/55/85/130/185/232; might R1-R6 =
// 20/35/55/85/115/155; greater might = 155/185. No Commanding Presence
// talent exists in Turtle DBC, so no multiplier half (unlike the donor).

#include <cstdint>

namespace ai
{
    inline int32_t BattleShoutAttackPower(uint32_t spellId)
    {
        switch (spellId)
        {
            case 6673: return 15;
            case 5242: return 35;
            case 6192: return 55;
            case 11549: return 85;
            case 11550: return 130;
            case 11551: return 185;
            case 25289: return 232;
            default: return 0;
        }
    }

    inline int32_t BlessingOfMightAttackPower(uint32_t spellId)
    {
        switch (spellId)
        {
            case 19740: return 20;
            case 19834: return 35;
            case 19835: return 55;
            case 19836: return 85;
            case 19837: return 115;
            case 19838:
            case 25782: return 155;
            case 25916: return 185;
            default: return 0;
        }
    }

    // hasMight: a might/greater-might aura is up; mightAp: its AP value.
    // Unknown spell ids map to 0 AP, which never suppresses a known shout.
    inline bool ShouldBattleShout(int32_t shoutAp, bool hasMight, int32_t mightAp)
    {
        if (!hasMight)
            return true;
        return shoutAp > mightAp;
    }
}
