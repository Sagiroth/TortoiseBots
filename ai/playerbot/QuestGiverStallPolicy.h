#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ai
{
    // Quest-giver stall back-off for masterless pool bots: a bot that walks
    // to a giver and finds nothing to take (TravelAction's giver-stall
    // release, QuestGiverStalled) re-picks the same giver on the next search
    // - the pick gate reads static template fit (level windows, area floors)
    // while the stall reads the live menu (chain prerequisites, taken status,
    // accept policy), and the two disagree for good. Live 2026-10-09 pool:
    // 14 of 139 chronically standing bots looped giver -> stall -> 30 s idle
    // expiry -> same giver (e.g. a level 7 priest walking to Apothecary
    // Johaan for Virulence|60113: MinLevel 6 but chained behind PrevQuestId
    // 367, whose own menu needs a finished chain the bot has not run - the
    // template windows fit, the menu never offers the quest).
    //
    // Second stall for one (giver entry, quest id) without the quest state
    // changing parks that pair for 30 min, so the next search picks another
    // giver. Per bot: the counter lives in the facade value store (the same
    // "quest hand in fail::<quest>" episode the unreachable-taker path uses),
    // the park in "manual time" (the same "no quest hand in until::<quest>"
    // pattern). Owned bots (HasActivePlayerMaster) are untouched - their
    // player walks them there. Quest-state change (accepted, completed,
    // rewarded, dropped from the log) clears the counter at the call site.
    //
    // Pure decision rule, no core includes: the caller owns the quest-status
    // read, the facade counter store and the manual-time park.

    // Stalls before the back-off engages: the first stall may be a race (menu
    // not yet built, quest just taken by another path), the second confirms
    // the giver has nothing for this bot.
    constexpr std::uint32_t kQuestGiverStallBackoffAfter = 2;

    // Back-off window: matches the stuck-hand-in and objective-stall parks
    // (AUTO_HAND_IN_PARK, kQuestStallParkSec) - long enough to quest
    // elsewhere, short enough to re-test after a ding.
    constexpr std::int64_t kQuestGiverBackoffParkSec = 30 * 60;

    // Facade counter key prefix (per giver + quest); the data field carries
    // the quest status observed at the last stall so a state change resets.
    inline std::string QuestGiverStallKey(std::int32_t giverEntry, std::uint32_t questId)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "quest giver stall::%d:%u", giverEntry, questId);
        return std::string(buf);
    }

    // Manual-time park key (per giver + quest): the pick gate skips this
    // pair while the timestamp stands. Same shape as the hand-in and
    // objective parks ("no quest hand in until::<quest>", "no quest
    // objective until::<quest>"), extended with the giver entry because the
    // stall is about this NPC's menu, not the quest everywhere.
    inline std::string QuestGiverBackoffKey(std::int32_t giverEntry, std::uint32_t questId)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "no quest giver until::%d:%u", giverEntry, questId);
        return std::string(buf);
    }

    // Back-off verdict over the caller's stall count: park once the pair has
    // stalled twice with no quest-state change between them.
    inline bool QuestGiverStallBacksOff(std::uint32_t stallCount)
    {
        return stallCount >= kQuestGiverStallBackoffAfter;
    }

    // Quest-state fingerprint the caller stores alongside the counter: any
    // change (take, progress, completion, reward, drop) breaks the "without
    // the quest state changing" chain and restarts the count at one. Kept as
    // a plain integer the facade data field round-trips (empty = unknown).
    inline bool QuestGiverStallStateChanged(std::uint32_t stored, bool hasStored, std::uint32_t current)
    {
        return hasStored && stored != current;
    }
}
