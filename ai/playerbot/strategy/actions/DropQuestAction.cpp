
#include "playerbot/playerbot.h"
#include "DropQuestAction.h"
#include "playerbot/QuestLogPolicy.h"

using namespace ai;

bool DropQuestAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string link = event.GetParam();
    if (!GetMaster())
        return false;

    PlayerbotChatHandler handler(GetMaster());
    uint32 entry = handler.extractQuestId(link);
    std::vector<uint32> questIds;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotIdCompat(bot, slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (questId == entry || link.find(quest->GetTitle()) != std::string::npos || link == "all")
        {
            questIds.push_back(questId);
            if (link != "all")
                break;
        }
    }

    for (uint32 questId : questIds)
    {
        ai->DropQuest(questId);
        ai->TellPlayer(requester, BOT_TEXT("quest_remove"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }

    return !questIds.empty();
}

bool CleanQuestLogAction::IsUpkeepBot()
{
    return sPlayerbotAIConfig.botQuestLogUpkeep &&
        bot && !ai->HasActivePlayerMaster() && sRandomBotFacade.IsRandomBot(bot);
}

bool CleanQuestLogAction::IsGreyIncomplete(Quest const* quest)
{
    // QuestLevel <= 0 is scaling: GetQuestLevelForPlayer falls back to bot
    // level, so such quests are never treated as grey.
    if (!quest || quest->GetQuestLevel() <= 0)
        return false;
    return bot->GetLevel() >= bot->GetQuestLevelForPlayer(quest) + 8;
}

bool CleanQuestLogAction::HasRequiredDeliverItems(Quest const* quest)
{
    if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER))
        return true;
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemCount[i] && bot->GetItemCount(quest->ReqItemId[i]) < quest->ReqItemCount[i])
            return false;
    return true;
}

bool CleanQuestLogAction::IsDroppable(Quest const* quest, QuestStatus status, bool upkeep, bool triage)
{
    if (!quest || quest->GetRequiredClasses())
        return false;
    // FAILED quests never complete on their own (the core parks them in
    // QUEST_STATE_FAIL): upkeep bots drop them instead of pinning the slot.
    // Donor NewRpgBaseAction::OrganizeQuestLog drops FAILED the same way.
    if (status == QUEST_STATUS_FAILED)
        return upkeep;
    if (status == QUEST_STATUS_INCOMPLETE)
    {
        if (IsGreyIncomplete(quest) && !quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER))
            return true;
        // Core reverts DELIVER quests to INCOMPLETE when the required items
        // are lost (Player::ItemRemovedQuestCheck), so plain !CanRewardQuest
        // on COMPLETE can never see them — they arrive here instead. Drop
        // only when grey AND the bot no longer holds the required items
        // (still re-farmable elsewhere, and the log slot is otherwise pinned
        // forever by a quest far below level). Money is never a drop reason.
        if (upkeep && IsGreyIncomplete(quest) && !HasRequiredDeliverItems(quest))
            return true;
        // Periodic triage for upkeep bots (donor OrganizeQuestLog idea, own
        // code): over-level (+3), elite/dungeon/raid (type != 0),
        // suggested-group and zone-mismatched quests are unfinishable solo
        // picks that only pin log slots. Triage runs only when the log is
        // nearly full (Execute gates it); grey quests are left to the rules
        // above and COMPLETE quests never reach here.
        if (triage && upkeep && ai::QuestTriageShouldDrop((int)quest->GetQuestLevel(), bot->GetLevel(),
            quest->GetType(), quest->GetSuggestedPlayers(), false,
            quest->GetZoneOrSort(), bot->GetZoneId()))
            return true;
        return false;
    }
    if (status == QUEST_STATUS_COMPLETE && upkeep && !bot->CanRewardQuest(quest, false))
    {
        if (quest->GetRewOrReqMoney() < 0 &&
            bot->GetMoney() < uint32(-quest->GetRewOrReqMoney()))
            return false;
        return true;
    }
    return false;
}

void CleanQuestLogAction::NoteCleanNoOp()
{
    // GetTrigger creates a fresh trigger when none is cached: use the
    // context only to reach the trigger node registry would need a new
    // accessor, so instead record the no-op on the bot itself. The trigger
    // consults it via the facade value store (same pattern as the hand-in
    // hysteresis in ChooseTravelTargetAction).
    sRandomBotFacade.SetValue(bot, "quest clean empty", 1, {}, 300);
}

bool CleanQuestLogAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (ai->HasActivePlayerMaster())
        return false;

    // The INCOMPLETE grey rule inside IsDroppable is the old behaviour and
    // always runs. The COMPLETE branch, the INCOMPLETE itemless-deliver
    // branch, the FAILED branch and the INCOMPLETE triage branch are
    // quest-log upkeep for masterless random bots only: IsUpkeepBot gates
    // on upkeep + no active master + IsRandomBot, so owned/alt bots never
    // lose quests even with the master offline. Triage runs only when the
    // log is nearly full (donor OrganizeQuestLog runs at freeSlotNum < 2);
    // otherwise the scan only applies the old per-quest rules.
    bool upkeep = IsUpkeepBot();
    bool dropped = false;
    // The triage gate mirrors QuestLogNearlyFullTrigger (free slots > 2 =
    // full enough to leave alone).
    uint8 freeSlots = 0;
    for (uint8 count = 0; count < MAX_QUEST_LOG_SIZE; ++count)
        if (!GetQuestSlotIdCompat(bot, count))
            ++freeSlots;
    bool triage = upkeep && freeSlots < 2;
    uint8 slot = 0;
    while (slot < MAX_QUEST_LOG_SIZE)
    {
        uint32 questId = GetQuestSlotIdCompat(bot, slot);
        QuestStatus status = questId ? bot->GetQuestStatus(questId) : QUEST_STATUS_NONE;
        if (!questId || (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE && status != QUEST_STATUS_FAILED))
        {
            ++slot;
            continue;
        }

        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        bool drop = IsDroppable(quest, status, upkeep, triage);

        ++slot;
        if (!drop)
            continue;

        ai->DropQuest(questId);
        ai->TellPlayer(requester, BOT_TEXT("quest_remove") + " " + chat->formatQuest(quest),
                       PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        dropped = true;
    }

    if (!dropped)
        NoteCleanNoOp();

    return dropped;
}
