#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the Razorgore fight (mod-playerbots parity,
// raid1 batch item 5).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/BWL/BWLTriggers.cpp:34-39
// (NotMindControlled: boss lacks SPELL_MINDCONTROL 19832) +
// BWLActions.cpp:59-106 (AvoidAoe: victim holds; in-cone within 15y steps
// behind the boss — melee to 3y, ranged holds 15y for War Stomp;
// ranged outside the cone but close backs off) + BWLActions.cpp:108-138
// (MarkBoss: off-tank moons boss while eggs live, clears after) +
// BWLMultipliers.cpp:20-41 (off-tank keeps boss via tank-assist veto while
// eggs live; non-victim tanks don't face into Cleave after) +
// BWLHelpers.h:21,40 (aura + egg GO 177807).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_razorgore_policy.cpp.

namespace ai
{
    // Razorgore 12435, Possess (orb MC) 19832, Black Dragon Egg GO 177807
    // (all 1.18.1 verified at port time — see provenance).
    constexpr std::uint32_t kRazorgoreEntry = 12435;
    constexpr std::uint32_t kPossessSpellId = 19832;
    constexpr std::uint32_t kBlackDragonEggEntry = 177807;

    // Donor geometry: 15y frontal 180-degree cone; ranged holds 15y (War
    // Stomp), melee works behind at 3y.
    constexpr float kRazorgoreConeRadius = 15.0f;
    constexpr float kRazorgoreRangedDistance = 15.0f;
    constexpr float kRazorgoreMeleeDistance = 3.0f;

    // Phase: while the boss lacks the Possess aura the orb controller has
    // not taken him yet — raid avoids the cone and the off-tank holds him.
    inline bool IsRazorgoreUncontrolled(bool bossHasPossessAura)
    {
        return !bossHasPossessAura;
    }

    // Cone escape: the victim holds (moving rotates the boss); everyone
    // else inside the frontal cone steps behind.
    inline bool ShouldEscapeRazorgoreCone(bool botIsVictim, bool insideCone)
    {
        return !botIsVictim && insideCone;
    }

    // Ranged outside the cone but inside 15y backs off (War Stomp).
    inline bool ShouldBackOffRazorgore(bool botIsRanged, bool botIsVictim, bool insideCone, bool insideRange)
    {
        return botIsRanged && !botIsVictim && !insideCone && insideRange;
    }

    // Off-tank moon + hold: first tank by group index holds the boss while
    // eggs live (bots never touch the orb — that stays a player job).
    inline bool ShouldHoldRazorgore(bool eggsAlive, bool botIsOffTank)
    {
        return eggsAlive && botIsOffTank;
    }
}
