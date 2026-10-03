#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai
{
    // POI-stall abandon for masterless pool bots (issue #423, donor E05):
    // mod-playerbots walks each quest objective's POI (NewRpgDoQuestAction::
    // DoIncompleteQuest/DoCompletedQuest), and when the bot has sat at the POI
    // for poiStayTime (5 min) with no objective progress it marks the quest
    // lowPriorityQuest and stops the pursuit (it does NOT remove the quest;
    // removal stays the nearly-full-log triage). Ours has no POI table to walk
    // - 1.12 ships no per-objective coordinates (no QuestPOIVector in core,
    // no quest-poi DBC/DB data) - but the pursuit itself already exists as
    // quest-objective travel (QuestObjectiveTravelDestination): the missing
    // half is the stall horizon. Without it a bot whose objective area yields
    // nothing is re-picked every minute (objectives expire fast) and walks
    // useless laps instead of moving on.
    //
    // Analog, adapted to our travel machine: while the bot is ARRIVED at the
    // objective (TravelTarget status WORK, i.e. inside the destination
    // radius) the quest's kill/item counters are anchored per quest; a later
    // WORK tick with unchanged counters past the horizon abandons the pursuit
    // for a park window (the quest search skips that quest's objectives, the
    // current target is nulled). Travel time never counts: the anchor is only
    // created and compared while WORK, so a long walk cannot consume the
    // horizon. The verdict accumulates across visits - objectives expire fast,
    // so one stay rarely spans 5 continuous WORK minutes; the anchor (30-min
    // TTL) bridges re-picks and the horizon measures total unproductive time
    // at the area. Time-boxed, not session-permanent like the donor's set, so
    // a later ding re-tests the quest instead of pinning its slot forever.
    // Existing level/area gates (#418, #428, #434) are untouched: gated
    // destinations are never picked, so they never anchor.
    //
    // Pure decision rule, no core includes: the caller owns the quest-status
    // read, the facade anchor store and the manual-time park.

    // Donor poiStayTime: 5 min at the POI before the no-progress verdict.
    constexpr std::int64_t kQuestStallHorizonSec = 5 * 60;

    // Abandoned pursuit park: matches the stuck-hand-in park
    // (AUTO_HAND_IN_PARK in MoveToTravelTargetAction.cpp).
    constexpr std::int64_t kQuestStallParkSec = 30 * 60;

    // Anchor lifetime: must bridge re-picks across visits to the same area, so
    // a stall accumulated over several short WORK stays is still recognized.
    // A stale anchor is harmless: firing still needs unchanged counters past
    // the horizon, which is a genuine stall however old the anchor is.
    constexpr int kQuestStallAnchorTtlSec = 30 * 60;

    struct QuestStallAnchor
    {
        std::int64_t time = 0;
        std::array<std::uint32_t, 4> kill = {};
        std::array<std::uint32_t, 4> item = {};
    };

    inline std::string FormatQuestStallAnchor(std::int64_t now,
        const std::array<std::uint32_t, 4>& kill,
        const std::array<std::uint32_t, 4>& item)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%lld %u %u %u %u %u %u %u %u",
            static_cast<long long>(now),
            kill[0], kill[1], kill[2], kill[3],
            item[0], item[1], item[2], item[3]);
        return std::string(buf);
    }

    inline bool ParseQuestStallAnchor(const std::string& data, QuestStallAnchor& anchor)
    {
        if (data.empty())
            return false;
        long long t = 0;
        unsigned k0 = 0, k1 = 0, k2 = 0, k3 = 0, i0 = 0, i1 = 0, i2 = 0, i3 = 0;
        if (std::sscanf(data.c_str(), "%lld %u %u %u %u %u %u %u %u",
            &t, &k0, &k1, &k2, &k3, &i0, &i1, &i2, &i3) != 9)
            return false;
        if (t <= 0)
            return false;
        anchor.time = static_cast<std::int64_t>(t);
        anchor.kill = {{k0, k1, k2, k3}};
        anchor.item = {{i0, i1, i2, i3}};
        return true;
    }

    // Counter-tracked objective: a kill or item requirement is present.
    // Exploration, area-trigger, event and spell objectives carry no kill or
    // item requirement and complete by other means - exempt from the stall
    // verdict. An id with zero count (or a count with zero id) requires no
    // progress either, so it never counts as trackable.
    inline bool QuestObjectiveTrackable(std::int32_t reqKillId, std::uint32_t reqKillCount,
        std::uint32_t reqItemId, std::uint32_t reqItemCount)
    {
        return (reqKillId != 0 && reqKillCount > 0) ||
            (reqItemId != 0 && reqItemCount > 0);
    }

    // Stall verdict: anchored pursuit of the quest, horizon elapsed since the
    // anchor (arrival) time, and none of the eight counters moved. Progress in
    // any counter refreshes the anchor at the call site instead. Per quest,
    // not per objective index: alternating picks between objectives of the
    // same quest share one anchor, so cycling stalled objectives still stalls.
    inline bool QuestObjectiveStalled(const std::array<std::uint32_t, 4>& curKill,
        const std::array<std::uint32_t, 4>& curItem,
        const std::array<std::uint32_t, 4>& anchorKill,
        const std::array<std::uint32_t, 4>& anchorItem,
        std::int64_t anchorTime, std::int64_t now,
        std::int64_t horizonSec = kQuestStallHorizonSec)
    {
        if (anchorTime == 0)
            return false;
        if (now - anchorTime < horizonSec)
            return false;
        return curKill == anchorKill && curItem == anchorItem;
    }
}
