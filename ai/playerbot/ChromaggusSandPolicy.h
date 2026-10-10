#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the Chromaggus Hourglass Sand cleanse
// (mod-playerbots parity, raid1 batch item 2).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/BWL/BWLTriggers.cpp:87-90
// (BwlAfflictionBronzeTrigger: self has SPELL_BROOD_AFFLICTION_BRONZE) +
// src/Ai/Raid/BWL/BWLActions.cpp (BwlUseHourglassSandAction: cast
// SPELL_HOURGLASS_SAND on self) + BWLStrategy.cpp (wiring).
// No core includes: the trigger/action translate game state into these
// plain inputs, so the rules stay testable in
// tools/test_chromaggus_sand_policy.cpp.

namespace ai
{
    // Chromaggus entry + Brood Affliction: Bronze / Hourglass Sand spell
    // ids (1.18.1 verified). Sand item is 19183 (casts 23645, stacks to
    // 200); the classic-21171 guess in the research report was wrong.
    constexpr std::uint32_t kChromaggusEntry = 14020;
    constexpr std::uint32_t kBronzeAfflictionSpellId = 23170;
    constexpr std::uint32_t kHourglassSandSpellId = 23645;
    constexpr std::uint32_t kHourglassSandItemId = 19183;

    // Trigger: the bot itself carries the Bronze affliction. Gated on a
    // live Chromaggus fight upstream (fight strategy), so the aura check
    // below stays the only per-tick cost.
    inline bool ShouldUseHourglassSand(bool selfHasBronzeAffliction)
    {
        return selfHasBronzeAffliction;
    }

    // The cleanse action name the strategy queues.
    inline bool IsHourglassSandAction(const std::string& actionName)
    {
        return actionName == "use hourglass sand";
    }
}
