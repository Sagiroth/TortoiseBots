#pragma once

// Pure decision rules for autonomous bot-only battlegrounds (i-queue follow-up).
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   RandomPlayerbotMgr::CheckBgQueue autonomous seeder
//   (src/Bot/RandomPlayerbotMgr.cpp:1162-1217, updateBGInstanceCount).
// Adapted: the donor fakes activeBgQueue=1 per (queue, bracket) up to a
// per-bracket count and lets the normal shouldJoinBg top-up fire, with a known
// over-queueing bug (donor conf comment). Here there is no census to fake —
// the service owns its queue entries — so the rule is a direct gate on three
// plain counts the service computes: running bot-only instances, owned bots
// already queued for the seed bracket, and eligible candidates. One BG type
// (WSG, smallest: 10v10), one bracket, one instance cap sized for an average
// PC. No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_autonomous_bg_policy.cpp.

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

    // Seed fill target per team: queue until each side can field a team.
    // The core starts the match when both sides have enough; extra bots past
    // the target are left for the demand path, never seeded.
    inline std::uint32_t AutonomousBgTeamTarget()
    {
        return kAutonomousBgTeamSize;
    }

    // Gate: seed this tick only when autonomous mode is on, no human demand
    // exists (demand path owns queueing then), running bot-only instances are
    // below the cap, and nothing already seeded is still waiting in queue.
    // Queued-but-unstarted seeds must start (or time out via the core) before
    // new ones are added — this is the anti-over-queue rule the donor lacks.
    inline bool ShouldSeedAutonomousBg(bool autonomousEnabled, bool hasHumanDemand,
        std::uint32_t runningBotOnlyInstances, std::uint32_t ownedQueuedForSeed,
        std::uint32_t maxInstances = kAutonomousBgMaxInstances)
    {
        if (!autonomousEnabled)
            return false;
        if (hasHumanDemand)
            return false;
        if (runningBotOnlyInstances >= maxInstances)
            return false;
        if (ownedQueuedForSeed > 0)
            return false;
        return true;
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
