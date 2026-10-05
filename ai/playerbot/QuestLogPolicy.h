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

    // Quest accept/drop churn (live pool: 16 695 accepts vs 7 194 drops).
    // The log showed two accept-side holes with the same shape as a drop
    // rule, so both are decided here on quest-template data and tested on
    // their own (donor NewRpgBaseAction::OrganizeQuestLog drops sort quests
    // ZoneOrSort < 0; modulated here to the unfillable turn-ins only):
    //
    // War-effort turn-ins: the AQ material quests (sort -365) with item
    // objectives are level-60 turn-ins a levelling bot can never fill -
    // 20 copper bars, 10 thick leather - so the bot accepted ~60/h in
    // Ironforge/Orgrimmar (3055 drops, top-30 all this sort) and the upkeep
    // pass dropped them again. Blocked at accept instead. The signet
    // quests (8846+) carry the same sort and the same fate (bots at 10-14,
    // quest items a level-10 bot cannot farm). Sort -365 quests with no
    // item objective (8792/8795 breadcrumb, 0 drops in the pool) stay open.
    inline bool IsWarEffortTurnIn(std::int32_t zoneOrSort, bool hasItemObjective)
    {
        return zoneOrSort == -365 && hasItemObjective;
    }


    // Novelty / deprecated quests the owner banned: CLUCK! (3861, a chicken
    // escort that pins a log slot for a joke reward) and inactive quest
    // templates (Method disabled, e.g. [DEPRECATED] 40298) never pay off.
    // Matched by id so live quests with the same shape are untouched.
    inline bool IsBannedQuest(std::uint32_t questId, bool inactive)
    {
        if (questId == 3861)
            return true;
        // Issue #492: world-buff unlock quests (90000-90013) are per-character
        // purchases for real players. Pool bots never take, hold or complete
        // them: the accept gate refuses, the drop rule cleans them, and the
        // aura/kill credit doors check the quest log (empty by construction).
        if (questId >= 90000 && questId <= 90013)
            return true;
        return inactive;
    }
    // One predicate for the accept gate and the drop rule, so the two cannot
    // drift: banned quests are refused by every bot; war-effort turn-ins
    // only by upkeep bots (a level-60 owned bot can actually fill them).
    inline bool ShouldRefuseQuestAtAccept(std::uint32_t questId, bool inactive,
        std::int32_t zoneOrSort, bool hasItemObjective, bool upkeepBot)
    {
        if (IsBannedQuest(questId, inactive))
            return true;
        if (upkeepBot && IsWarEffortTurnIn(zoneOrSort, hasItemObjective))
            return true;
        return false;
    }

    // Bone Chew Toy (item 51751, GO 1000380 near Goldshire): the only quest
    // needing it is the inactive 40298, so every copy in bags is junk. Loot
    // and bag rules key off these ids directly - a generic "quest-class
    // item not in log" purge would also eat quest starters (Free Ticket
    // Voucher 19338 etc.), which must be kept.
    constexpr std::uint32_t kBoneChewToyItemId = 51751;
    constexpr std::uint32_t kBoneChewToyGoEntry = 1000380;
}
