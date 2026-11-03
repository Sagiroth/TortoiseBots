#pragma once

// Rules for a bot a creature has charmed (Hakkar's Cause Insanity 24327, Will of Hakkar 24178) and for the area
// actions the raid must not put on such a member. Tested in tools/test_charm_yield_policy.cpp; callers pass plain
// numbers, so this header stays free of core includes.
//
// Why a creature's charm is different from a player's: the core's charm handler (SpellAuras.cpp HandleModCharm) gives a
// player charmer a PlayerControlledAI of its own, while a creature charmer leaves the bot's PlayerbotAI installed and
// only calls SetControlledBy. Without the yield both AIs drive the same player. ServerFacade::IsCharmed cannot answer
// it: its MANGOS branch counts player charmers only, and other callers depend on that.

#include <cstdint>

namespace ai
{
    // The area radius a charmed group member keeps clear: Corrupted Blood 24328 splashes 10 yd, and one ally's blast
    // on a 5.8 k hp charmed tank is what the veto exists for.
    constexpr float kCharmedAllyAreaYd = 10.0f;

    // Whether the bot's own AI yields this update: the key is on, a creature charmed it, and a player did not.
    inline bool CreatureCharmYields(bool keyOn, bool hasCharmer, bool charmerIsPlayer)
    {
        return keyOn && hasCharmer && !charmerIsPlayer;
    }

    // Whether an area action stands down for the charmed member: its effect point (or the bot itself, for a
    // self-centred blast) lies within kCharmedAllyAreaYd of the member. `allyToPointYd` is only read for a
    // point-centred action and `allyToBotYd` only for a self-centred one.
    inline bool CharmedAllyInArea(float allyToPointYd, float allyToBotYd, bool selfCentred)
    {
        return (selfCentred ? allyToBotYd : allyToPointYd) <= kCharmedAllyAreaYd;
    }
}
