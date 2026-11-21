#pragma once

#include <cstdint>

// Opt-in Holy Paladin melee-weaving (AiPlayerbot.HolyPaladinMeleeWeave,
// default 0). When enabled, a grouped Holy Paladin whose groupmates are all
// at or above the medium-health threshold (nothing more urgent than the
// light-heal band) walks into melee of the tank's target, keeps Seal of
// Wisdom up, auto-attacks, and casts Holy Strike on cooldown (falling back
// to Crusader Strike only if Holy Strike is unknown).
//
// No core includes: callers translate game state into plain inputs.
namespace TortoiseBots
{

struct HolyMeleeWeaveInput
{
    bool enabled = false;
    bool isHolyPaladin = false;
    bool inGroup = false;
    bool hasTarget = false;
    bool targetAlive = false;
    bool targetIsPlayer = false;
    bool targetIsRaidBoss = false;
    bool targetEngaged = false;
    uint8_t selfHealthPct = 100;
    uint8_t partyMemberToHealPct = 100;
    uint8_t mediumHealthPct = 70;
};

inline bool HolyMeleeWeaveAllowed(HolyMeleeWeaveInput const& in)
{
    if (!in.enabled || !in.isHolyPaladin || !in.inGroup)
        return false;
    if (!in.hasTarget || !in.targetAlive || in.targetIsPlayer || in.targetIsRaidBoss || !in.targetEngaged)
        return false;
    if (in.selfHealthPct < in.mediumHealthPct || in.partyMemberToHealPct < in.mediumHealthPct)
        return false;
    return true;
}

enum class HolyMeleeStrikePick : uint8_t
{
    None = 0,
    HolyStrike,
    CrusaderStrikeFallback,
};

inline HolyMeleeStrikePick PickHolyMeleeStrike(bool weaveEnabled, bool knowsHolyStrike, bool knowsCrusaderStrike)
{
    if (!weaveEnabled)
        return knowsCrusaderStrike ? HolyMeleeStrikePick::CrusaderStrikeFallback : HolyMeleeStrikePick::None;
    if (knowsHolyStrike)
        return HolyMeleeStrikePick::HolyStrike;
    if (knowsCrusaderStrike)
        return HolyMeleeStrikePick::CrusaderStrikeFallback;
    return HolyMeleeStrikePick::None;
}

} // namespace TortoiseBots
