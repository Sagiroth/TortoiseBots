#pragma once

// Free hires for staff accounts. Pure, core-free policy for the hire fee:
// AiPlayerbot.HireFreeMinSecurity (default 0 = off) names the account security
// level from which a hire costs nothing: no gold is taken and the reply says
// "free (GM)". Nothing else changes: the quote, the caps and the level rules
// stay as they are, and no gold is ever added to the player.
// Unit-tested standalone (tools/test_hire_fee_policy.cpp). The provisioner and
// the recruiter menus both read this header, so the rule cannot drift.

#include <cstdint>

namespace TortoiseBots
{

// AiPlayerbot.HireFreeMinSecurity as read from the config (signed): below 0
// is 0 (off), above 6 is 6.
inline uint32_t ClampHireFreeMinSecurity(int64_t raw)
{
    if (raw < 0)
        return 0;
    if (raw > 6)
        return 6;
    return static_cast<uint32_t>(raw);
}

// True when an account of this security level hires for free. 0 turns the
// policy off for everyone, including level 0 accounts.
inline bool HireFeeWaived(int accountSecurity, uint32_t freeMinSecurity)
{
    return freeMinSecurity != 0 && accountSecurity >= static_cast<int>(freeMinSecurity);
}

// The fee actually charged: 0 when waived, the quoted fee otherwise.
inline uint32_t HireFeeCharged(uint32_t quotedCopper, int accountSecurity, uint32_t freeMinSecurity)
{
    return HireFeeWaived(accountSecurity, freeMinSecurity) ? 0 : quotedCopper;
}

} // namespace TortoiseBots
