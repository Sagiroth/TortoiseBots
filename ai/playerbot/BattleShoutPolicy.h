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
// shout R1-R7 = 15/35/55/85/130/185/232; might R1-R7 =
// 20/35/55/85/115/155/185; greater might = 155/185. The bot's own shout AP
// is scaled by its Improved Battle Shout talent rank (+5%/rank, spell ids
// 12318/12857/12858/12860/12861 — the donor's COMMANDING_PRESENCE_RANKS
// are these same Vanilla ids under the WotLK name).

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
            case 25291:
            case 25916: return 185;
            default: return 0;
        }
    }

    // Improved Battle Shout talent bonus from the spell id of the talent
    // aura on the bot (R1-R5 = +5%/+10%/+15%/+20%/+25%). Unknown/zero id =
    // untalented. Highest rank wins if several aura rows are present.
    inline float ImprovedBattleShoutBonus(uint32_t spellId)
    {
        switch (spellId)
        {
            case 12318: return 0.05f;
            case 12857: return 0.10f;
            case 12858: return 0.15f;
            case 12860: return 0.20f;
            case 12861: return 0.25f;
            default: return 0.0f;
        }
    }

    // Effective shout AP after the talent multiplier (donor: BasePoints+1
    // scaled by 1+bonus, truncated to int).
    inline int32_t EffectiveBattleShoutAp(int32_t baseAp, float talentBonus)
    {
        return int32_t(float(baseAp) * (1.0f + talentBonus));
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
