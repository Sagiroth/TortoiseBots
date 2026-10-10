#pragma once

// Pure decision rules for autonomous bot-only battlegrounds (i-queue follow-up).
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   RandomPlayerbotMgr::CheckBgQueue autonomous seeder
//   (src/Bot/RandomPlayerbotMgr.cpp:1162-1217, updateBGInstanceCount).
// Adapted: the donor fakes activeBgQueue=1 per (queue, bracket) up to a
// per-bracket count and lets the normal shouldJoinBg top-up fire. Here there
// is no census to fake — the service owns its queue entries — so the rule is
// need-based top-up: keep queueing while a team is below target, stop when
// full. One BG type (WSG, smallest: 10v10), one bracket, one instance cap
// sized for an average PC. No core includes: callers translate game state into
// plain inputs so the rules stay testable in tools/test_autonomous_bg_policy.cpp.

#include <cstdint>

namespace TortoiseBots
{
    // Warsong Gulch needs 20 bots (10v10) — the smallest BG. AB needs 30,
    // AV up to 80. Autonomous seeding stays WSG-only so one match costs the
    // pool 20 bots, not a raid.
    constexpr std::uint32_t kAutonomousBgTeamSize = 10;

    // Cap sized for an average PC: at most this many concurrent bot-only BG
    // instances. 1 WSG = 20 bots fighting on an already-loaded map; the
    // demand-driven backfill path is unaffected and may run alongside.
    constexpr std::uint32_t kAutonomousBgMaxInstances = 1;

    // Clamp for the max-instances knob: 0 disables via the gate below, and
    // anything above 2 is an average-PC risk (2 full WSG = 40 fighting bots).
    constexpr std::uint32_t kAutonomousBgMaxInstancesLimit = 2;

    inline std::uint32_t ClampAutonomousMaxInstances(std::uint32_t configured)
    {
        if (configured > kAutonomousBgMaxInstancesLimit)
            return kAutonomousBgMaxInstancesLimit;
        return configured;
    }

    // Seed fill target per team: queue until each side can field a team.
    // The core starts the match when both sides reach min (4v4) and fills to
    // 10v10; extra bots past the target are left for the demand path.
    inline std::uint32_t AutonomousBgTeamTarget()
    {
        return kAutonomousBgTeamSize;
    }

    // Gate: seed this tick only when autonomous mode is on, no human demand
    // exists (demand path owns queueing then), FULL bot-only instances of THIS
    // BRACKET are below the (clamped) cap, and at least one team still needs
    // bots. Queued seeds do NOT block, and neither does a still-forming match:
    // batches of maxPerInterval accumulate across ticks until both teams reach
    // target (donor-style top-up), and the core's free-slot path invites the
    // queued seeds into the forming match. Only a full instance (both sides at
    // max, closed to top-up) counts as running. Over-queueing is bounded by the
    // per-team target, not by a first-seed latch.
    inline bool ShouldSeedAutonomousBg(bool autonomousEnabled, bool hasHumanDemand,
        std::uint32_t runningBotOnlyInstances, bool anyTeamNeedsBots,
        std::uint32_t maxInstances = kAutonomousBgMaxInstances)
    {
        if (!autonomousEnabled)
            return false;
        if (hasHumanDemand)
            return false;
        if (runningBotOnlyInstances >= ClampAutonomousMaxInstances(maxInstances))
            return false;
        return anyTeamNeedsBots;
    }

    // Bracket readiness: both factions must have enough eligible bots to form
    // a match. A 19/1 bracket never seeds (it could never reach core min).
    inline bool BracketCanSeed(std::uint32_t eligibleAlliance, std::uint32_t eligibleHorde,
        std::uint32_t teamTarget = kAutonomousBgTeamSize)
    {
        return eligibleAlliance >= teamTarget && eligibleHorde >= teamTarget;
    }

    // Per-team gate: keep queueing this team while its seeded count (queued +
    // already inside bot-only instances of this bracket) is below the team
    // target. Stops exactly at 10v10 so seeds never pile into 2v1 brackets.
    inline bool TeamNeedsSeedBots(std::uint32_t seededForTeam,
        std::uint32_t teamTarget = kAutonomousBgTeamSize)
    {
        return seededForTeam < teamTarget;
    }
} // namespace TortoiseBots
