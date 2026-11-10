#pragma once

// AiPlayerbot.BossTacticsKeepsEngagedBoss: the raid's engaged bosses, kept in a bot's fight after its own attacker
// list lost them. A melee bot that cannot reach an airborne boss gives up on it (AttackersValue::IgnoreTarget), so
// the boss vanishes from that bot's attacker list: no boss-tactic row and no stand seat for it. The bots that still
// see the boss note it in a registry (BossTacticsActions.cpp), and a bot that lost it reads it back while the
// registry entry is fresh, the boss still fights, and the bot itself is in combat within reach.
//
// No core includes: the translation reads the game state into EngagedBossView, and the rules stay testable in
// tools/test_boss_engaged_policy.cpp.

#include <cstdint>

namespace ai
{
    // A boss no bot saw in its attackers for this long is dropped.
    constexpr std::uint32_t kEngagedBossFreshMs = 10000;
    // At most one registry write per boss per half second.
    constexpr std::uint32_t kEngagedBossNoteMs = 500;
    // The bot reads a registered boss within this (3D).
    constexpr float kEngagedBossReachYd = 150.0f;

    inline bool EngagedBossNoteDue(bool have, std::uint32_t nowMs, std::uint32_t lastSeenMs)
    {
        return !have || static_cast<std::uint32_t>(nowMs - lastSeenMs) >= kEngagedBossNoteMs;
    }

    struct EngagedBossView
    {
        bool known = false;
        std::uint32_t lastSeenMs = 0;
        bool alive = false;
        bool inCombat = false;
        bool evading = false;
        bool botInCombat = false;
        float botToBossYd = 0.0f;
    };

    inline bool EngagedBossUsable(const EngagedBossView& v, std::uint32_t nowMs)
    {
        return v.known && static_cast<std::uint32_t>(nowMs - v.lastSeenMs) < kEngagedBossFreshMs && v.alive &&
               v.inCombat && !v.evading && v.botInCombat && v.botToBossYd <= kEngagedBossReachYd;
    }
}
