#pragma once

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
    // nothing re-picks the same trip every minute (objectives expire fast)
    // and walks useless laps instead of moving on.
    //
    // Analog, adapted to our travel machine: each objective pick anchors the
    // quest's progress counters; a later pick of the same quest+objective
    // with unchanged counters past the horizon abandons the pursuit for a
    // park window (fetch skips that quest's objectives, the just-made pick is
    // expired). Time-boxed, not session-permanent like the donor's set, so a
    // later ding re-tests the quest instead of pinning its slot forever.
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

    // Anchor lifetime: must bridge consecutive picks across the 10-min quest
    // purpose park, so a stall is still recognized after a quiet spell. A
    // stale anchor is harmless: firing still needs unchanged counters past
    // the horizon, which is a genuine stall however old the anchor is.
    constexpr int kQuestStallAnchorTtlSec = 30 * 60;

    struct QuestStallAnchor
    {
        std::int64_t time = 0;
        std::uint32_t objective = 0;
        std::uint32_t kill = 0;
        std::uint32_t item = 0;
    };

    inline std::string FormatQuestStallAnchor(std::int64_t now, std::uint32_t objective,
        std::uint32_t kill, std::uint32_t item)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%lld %u %u %u",
            static_cast<long long>(now), objective, kill, item);
        return std::string(buf);
    }

    inline bool ParseQuestStallAnchor(const std::string& data, QuestStallAnchor& anchor)
    {
        if (data.empty())
            return false;
        long long t = 0;
        unsigned o = 0, k = 0, i = 0;
        if (std::sscanf(data.c_str(), "%lld %u %u %u", &t, &o, &k, &i) != 4)
            return false;
        if (t <= 0)
            return false;
        anchor.time = static_cast<std::int64_t>(t);
        anchor.objective = o;
        anchor.kill = k;
        anchor.item = i;
        return true;
    }

    // Stall verdict: anchored pursuit of the same objective, horizon elapsed,
    // neither counter moved. Progress in either counter refreshes the anchor
    // at the call site instead.
    inline bool QuestObjectiveStalled(std::uint32_t curKill, std::uint32_t curItem,
        std::uint32_t anchorKill, std::uint32_t anchorItem,
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
