#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include <cstdint>
#include <vector>

namespace ai
{
    // "hold kill blockers::<entry>": how many living units of the blocker entry a hold_kill row waits for (a fighting
    // Mad Voidwalker, a Portal of Madness) stand within kHoldKillRangeYd of the bot. The one grid search of the
    // hold_kill tactic, run at most once a second per bot and blocker entry: every reader (the dps target pickers, the
    // hold multiplier, the trigger and the action) goes through this value, never through a search of its own.
    class HoldKillBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        HoldKillBlockersValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "hold kill blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };

    // "fighting blockers::<entry>::<radius>": how many living units of a gate_hold row's blocker entry within `radius`
    // yards of the bot fight (in combat and not evading: CountsAsFightingBlocker). Never the hold_kill rule, which also
    // counts an unselectable unit that never fights: an evaded second boss back in the air is out of the encounter and
    // must not hold the spared one. One grid search per bot, entry and radius a second; every gate reader (the pickers,
    // the veto and the hold action) goes through this value.
    class GateHoldFightingBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        GateHoldFightingBlockersValue(PlayerbotAI* ai)
            : CalculatedValue<uint32>(ai, "fighting blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };

    // "sole kill candidates::<entry>": the living units of the entry within kHoldKillRangeYd that fight, the whole
    // fight's units of that entry (the pick rule is BossTacticsPolicy's PickSoleKill: lowest health, lowest guid on a
    // tie, among the `eligible` ones; a crowd-controlled or evading unit stays on the list, ineligible, for the area
    // veto). Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark, so every bot
    // of the fight reads the same set. The same grid search as the blocker count, at most once a second per bot and
    // entry.
    class SoleKillCandidatesValue : public CalculatedValue<std::vector<SoleCandidate>>, public Qualified
    {
    public:
        SoleKillCandidatesValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<SoleCandidate>>(ai, "sole kill candidates", 1), Qualified() {}
        std::vector<SoleCandidate> Calculate() override;
    };

    // "kill first grid units::<entry>": every living creature of the entry within kBossTacticsEngageRange of the bot,
    // with what the kill_first grid rule needs (KillFirstGridUnit). Its reader is FindBossKillFirst: a kill_first unit
    // the bot's attackers list never shows (a healing ward on the boss, a Shade it cannot see) is still found here.
    // Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark. One grid search per
    // bot and entry per second.
    class KillFirstGridUnitsValue : public CalculatedValue<std::vector<KillFirstGridUnit>>, public Qualified
    {
    public:
        KillFirstGridUnitsValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<KillFirstGridUnit>>(ai, "kill first grid units", 1), Qualified() {}
        std::vector<KillFirstGridUnit> Calculate() override;
    };

    // "tank post units::<entry>": every creature of the awaited entry within kTankPostSearchYd of the bot, alive or
    // dead, with the flags the tank_post arrival read needs (TankPostUnit). The same grid search as the blocker count,
    // at most once a second per bot and entry; the pick (PickTankPostUnit) is the policy's.
    class TankPostUnitsValue : public CalculatedValue<std::vector<TankPostUnit>>, public Qualified
    {
    public:
        TankPostUnitsValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<TankPostUnit>>(ai, "tank post units", 1), Qualified() {}
        std::vector<TankPostUnit> Calculate() override;
    };

    // "add crew::<boss>": AiPlayerbot.DungeonRunAddCrew's state for this bot, once the map's add_crew row for the
    // qualifier boss exists and its switch unit (`target_entry`) no longer stands alive: Waiting (the key or the row is
    // off, or the switch unit still lives: today's add duty stands), Crew (melee, tanks, and the first `priority` AoE
    // ranged bots of the raid) or NotCrew (a ranged damage dealer past the crew: it stays on the boss). The switch unit
    // is read through the cached "tank post units" search (an empty list counts as dead: a corpse that despawned).
    // Written for the uint8 enum (AddCrewState): 0 Waiting, 1 Crew, 2 NotCrew.
    class AddCrewValue : public CalculatedValue<std::uint8_t>, public Qualified
    {
    public:
        AddCrewValue(PlayerbotAI* ai) : CalculatedValue<uint8>(ai, "add crew", 2), Qualified() {}
        uint8 Calculate() override;
    };
}
