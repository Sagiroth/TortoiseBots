#pragma once

#include <cstdint>
#include <ctime>

// Pure decision rule for the mage flamestrike -> blizzard sequencing port
// (MAG-2): mod-playerbots stacks an instant flamestrike under the pack and
// then channels blizzard on top of the burning ground
// (`GenericMageStrategy.cpp:182-186,196,202-205`: `medium aoe` ->
// flamestrike 23 then blizzard 22, plus `flamestrike active and medium
// aoe` -> blizzard 24).
//
// The donor's "flamestrike active" check reads its own flamestrike
// DynamicObject within 30yd (`MageTriggers.cpp:107-124`, via
// `Aura::GetDynobjOwner`). That link does not exist in this core: our
// `NearestDynamicObjects` value is an empty stub (no dynobj grid searcher)
// and `Aura` exposes no dynobj owner. So the port tracks "I just cast
// flamestrike" instead: the `last spell cast` value (already maintained by
// `PlayerbotAI::CastSpell` for every cast) feeds this rule, and callers in
// strategy/ translate game state into these plain inputs. The rule stays
// testable in tools/test_flamestrike_window_policy.cpp without the server.
//
// Flamestrike's ground effect lasts 8s in 1.12; the window below keeps the
// follow-up blizzard inside the burning ground with margin for the GCD and
// one failed cast. Donor ordering (blizzard-on-active 24 > flamestrike 23
// > blizzard 22) is preserved by the strategy rows, not here.

namespace ai
{
    // Flamestrike cast spell ids (the cast, not the 2120-line effects):
    // 2124/2125 rank 1-2, 8425/8426 rank 3-4, 10217/10218 rank 5-6.
    inline bool IsFlamestrikeCastId(std::uint32_t spellId)
    {
        return spellId == 2124 || spellId == 2125 || spellId == 8425 ||
               spellId == 8426 || spellId == 10217 || spellId == 10218;
    }

    struct FlamestrikeWindowState
    {
        std::uint32_t lastCastSpellId;   // `last spell cast` value: spell just cast
        time_t lastCastTime;      // `last spell cast` value: when it landed
        time_t now;               // current time
        bool packStillGrouped;    // the medium-aoe trigger still holds
    };

    // Seconds of the flamestrike ground effect during which a blizzard
    // follow-up still stacks on burning ground.
    constexpr time_t FLAMESTRIKE_WINDOW_SECONDS = 6;

    // True when the bot cast flamestrike moments ago and the pack is still
    // grouped: channel blizzard on top instead of casting flamestrike again.
    inline bool ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState const& state)
    {
        if (!state.packStillGrouped)
            return false;
        if (!IsFlamestrikeCastId(state.lastCastSpellId))
            return false;
        if (state.now < state.lastCastTime)
            return false;
        return (state.now - state.lastCastTime) <= FLAMESTRIKE_WINDOW_SECONDS;
    }
}
