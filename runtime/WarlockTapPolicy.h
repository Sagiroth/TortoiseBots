#pragma once

#include <cstdint>

// Warlock Life Tap top-up rule (WAR-5, mod-playerbots parity): the donor
// taps whenever mana drops below 85% with health above its low-health
// line, as a low-priority filler (relevance 5.1) and out-of-combat pre-tap,
// while keeping an urgent band for near-empty mana. Ours only fired at
// mana<=mediumMana (default 40), so a warlock entered every pull at whatever
// mana the last fight left and spent the second half wanding.
//
// Two bands, both gated on health above the low-health line:
//   - TapUrgent:  mana <= mediumMana (the live combat row, NORMAL+2).
//   - TapTopUp:   mana < 85 (filler NORMAL-1 combat / NORMAL non-combat),
//                 never preempting dot upkeep or the urgent row.
// The rule is a pure function of mana/health percents plus the two config
// lines and is unit-tested on its own
// (tools/test_warlock_tap_policy.cpp). The health floor stays ours
// (lowHealth, default 50 — stricter than the donor's 45). Affliction's
// Dark Pact on low mana is untouched and still wins the emergency.

namespace TortoiseBots
{

inline constexpr uint8_t WARLOCK_TAP_TOPUP_MANA = 85;

enum class WarlockTapDecision
{
    LeaveAlone,
    TapUrgent,
    TapTopUp,
};

struct WarlockTapInputs
{
    bool knowsLifeTap = false; // HasSpell("life tap")
    uint8_t manaPct = 100;
    uint8_t healthPct = 100;
    uint8_t mediumMana = 40;   // AiPlayerbot.MediumMana
    uint8_t lowHealth = 50;    // AiPlayerbot.LowHealth
};

inline WarlockTapDecision DecideWarlockTap(WarlockTapInputs const& inputs)
{
    if (!inputs.knowsLifeTap)
        return WarlockTapDecision::LeaveAlone;
    if (inputs.healthPct <= inputs.lowHealth)
        return WarlockTapDecision::LeaveAlone;
    if (inputs.manaPct <= inputs.mediumMana)
        return WarlockTapDecision::TapUrgent;
    if (inputs.manaPct < WARLOCK_TAP_TOPUP_MANA)
        return WarlockTapDecision::TapTopUp;
    return WarlockTapDecision::LeaveAlone;
}

} // namespace TortoiseBots
