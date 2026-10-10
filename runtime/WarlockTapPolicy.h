#pragma once

#include <cstdint>

// Warlock Life Tap top-up rule (WAR-5, mod-playerbots parity): the donor
// taps whenever mana drops below 85% with health above its low-health
// line. Ours keeps that band as an out-of-combat pre-tap only: a combat
// filler is impossible in our engine (defaults are pushed at
// relevance-200, so any trigger row preempts the nuke), while the urgent
// band (mana<=mediumMana) stays live in combat. The health floor stays
// ours (lowHealth, default 50 — stricter than the donor's 45).
// Affliction's Dark Pact on low mana is untouched and still wins the
// emergency. The rule is a pure function of mana/health percents plus the
// two config lines and is unit-tested on its own
// (tools/test_warlock_tap_policy.cpp).

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
    // Out of combat the bot eats first, then taps to full: while a food or
    // drink aura is up (mid-meal) both bands stay quiet so the tap never
    // stands the bot up and resets its regen. Combat ignores the flag.
    bool isEating = false;
};

inline WarlockTapDecision DecideWarlockTap(WarlockTapInputs const& inputs, bool inCombat = true)
{
    if (!inputs.knowsLifeTap)
        return WarlockTapDecision::LeaveAlone;
    // Callers pass false out of combat; then a mid-meal bot is left alone so
    // the tap row (relevance 9, above food/drink at 6) never stands it up and
    // resets its regen. In combat eating never applies, flag ignored.
    if (!inCombat && inputs.isEating)
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
