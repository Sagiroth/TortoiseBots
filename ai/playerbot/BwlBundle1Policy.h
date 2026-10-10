#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the cheap BWL per-boss bundle (mod-playerbots
// parity, raid1 batch item 4): Broodlord 18y ranged step-out, drake
// off-tank flank, Nefarian mage Ice Block on Wild Magic, Vael 13020 in the
// dragon-flank list.
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/BWL/BWLTriggers.cpp:59-71
// (BwlBroodlordRangedTooCloseTrigger: ranged non-victim within
// BROODLORD_SAFE_DISTANCE 18y), BWLActions.cpp:269-278 (step out to 18y),
// BWLTriggers.cpp:102-106 (BwlNefarianWildMagicTrigger: mage + Wild Magic
// 23410), BWLStrategy.cpp:38-47/54-55 (drake rear flank + ice block wiring),
// BWLHelpers.h:55 (18y constant).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_bwl_bundle1_policy.cpp.

namespace ai
{
    // Broodlord Lashlayer 12017, Vaelastrasz the Corrupt 13020,
    // Nefarian 11583, Wild Magic 23410 (all 1.18.1 verified).
    constexpr std::uint32_t kBroodlordEntry = 12017;
    constexpr std::uint32_t kVaelEntry = 13020;
    constexpr std::uint32_t kNefarianEntry = 11583;
    constexpr std::uint32_t kWildMagicSpellId = 23410;

    // Donor BROODLORD_SAFE_DISTANCE: ranged holds 18y (Blast Wave).
    constexpr float kBroodlordRangeDistance = 18.0f;

    // Broodlord: ranged non-victims inside 18y step out. Victims (aggro)
    // must not kite the boss through the room.
    inline bool ShouldLeaveBroodlordRange(bool botIsRanged, bool botIsVictim, bool insideRange)
    {
        return botIsRanged && !botIsVictim && insideRange;
    }

    // Drake flank for off-tanks: a tank that is NOT the boss's victim
    // flanks like everyone else instead of holding the head.
    inline bool ShouldOffTankFlank(bool botIsTank, bool botIsVictim)
    {
        return botIsTank && !botIsVictim;
    }

    // Nefarian Wild Magic: mage carriers Ice Block (existing action).
    inline bool ShouldNefarianIceBlock(bool botIsMage, bool selfHasWildMagic)
    {
        return botIsMage && selfHasWildMagic;
    }
}
