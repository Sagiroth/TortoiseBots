#pragma once

#include <cstdint>

namespace ai
{
    // Periodic quest-log triage for masterless pool bots (donor parity, E07):
    // mod-playerbots drops not-worth-doing / not-capable / FAILED /
    // zone-mismatched quests when the log runs low (NewRpgBaseAction::
    // OrganizeQuestLog, IsQuestWorthDoing, IsQuestCapableDoing) instead of
    // only gating at accept. Ours gated at accept only
    // (AcceptQuestAction::WouldAcceptQuest), so the log clogged with
    // unfinishable quests pinning slots - FAILED quests in particular were
    // invisible to both the clean action and its trigger pre-scan. The rule
    // below is pure data so it is decided here and tested on its own.
    //
    // Deliberate divergences from the donor's low-slot passes: COMPLETE
    // quests are never triaged (a finished quest is finishable by
    // definition - turn it in, don't drop it), class quests stay preserved
    // at the call site, and there is no "clear the whole log" last resort.
    // Grey quests are handled by the existing IsGreyIncomplete rule, not
    // here. Repeatable/seasonal drops are not ported: no 1.12 seasonal API
    // exists and repeatables are outside the brief.
    //
    // Donor numbers kept: +3 over-level, type != 0 (elite/dungeon/raid/...),
    // suggested >= 2. ZoneOrSort > 0 is a zone id: a quest belonging to a
    // zone the bot is not in is dropped; <= 0 is a sort id
    // (class/profession/...) or unknown and never a zone reason.
    // QuestLevel 0 is scaling: never over-level. botZoneId 0 is unknown:
    // never a zone reason.
    inline bool QuestTriageShouldDrop(int questLevel, std::uint32_t botLevel,
        std::uint32_t questType, std::uint32_t suggestedPlayers,
        bool failed, std::int32_t zoneOrSort, std::uint32_t botZoneId)
    {
        if (failed)
            return true;

        if (questLevel > 0 && (int)botLevel + 3 < questLevel)
            return true;

        if (questType != 0)
            return true;

        if (suggestedPlayers >= 2)
            return true;

        if (zoneOrSort > 0 && botZoneId != 0 && zoneOrSort != (std::int32_t)botZoneId)
            return true;

        return false;
    }
}
