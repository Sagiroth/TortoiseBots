#include "Config/Config.h"

#include "playerbot/playerbot.h"
#include "TalkToQuestGiverAction.h"
#include "playerbot/QuestRewardPolicy.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/GuildValues.h"

using namespace ai;

bool TalkToQuestGiverAction::ProcessQuest(Player* requester, Quest const* quest, WorldObject* questGiver)
{
    // Challenge quests (Hardcore mode, etc.) - must never be processed by bots.
    if (quest->GetQuestId() == 80388)
        return false;

    if (questGiver && (questGiver->GetEntry() == 81030 || questGiver->GetEntry() == 62609))
        return false;

    bool isCompleted = false;

    std::string outputMessage;
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    QuestStatus status = bot->GetQuestStatus(quest->GetQuestId());
    if (sPlayerbotAIConfig.syncQuestForPlayer)
    {
        if (requester && (!PlayerbotAIStorage::Instance().GetAI(requester) || PlayerbotAIStorage::Instance().GetAI(requester)->IsRealPlayer()))
        {
            QuestStatus masterStatus = requester->GetQuestStatus(quest->GetQuestId());
            if (masterStatus == QUEST_STATUS_INCOMPLETE || masterStatus == QUEST_STATUS_FAILED)
            {
                isCompleted |= CompleteQuest(requester, quest->GetQuestId());
            }
        }
    }

    if (sPlayerbotAIConfig.syncQuestWithPlayer)
    {
        if (requester && requester->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE && (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_FAILED))
        {
            isCompleted |= CompleteQuest(bot, quest->GetQuestId());
            status = bot->GetQuestStatus(quest->GetQuestId());
        }
    }

    switch (status)
    {
        case QUEST_STATUS_COMPLETE:
        {
            isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);
            break;
        }
        case QUEST_STATUS_INCOMPLETE:
        {
            outputMessage = BOT_TEXT2("quest_status_incomplete", args);
            break;
        }
        case QUEST_STATUS_AVAILABLE:
        case QUEST_STATUS_NONE:
        {
            if (quest->IsAutoComplete() && bot->CanTakeQuest(quest, false) && bot->CanRewardQuest(quest, false))
                isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);
            else
                outputMessage = BOT_TEXT2("quest_status_available", args);
            break;
        }
        case QUEST_STATUS_FAILED:
        {
            outputMessage = BOT_TEXT2("quest_status_failed", args);
            break;
        }
    }

    ai->TellPlayer(requester, outputMessage, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return isCompleted;
}

bool TalkToQuestGiverAction::RewardFinishedQuest(PlayerbotAI* ai, Quest const* quest, WorldObject* questGiver)
{
    if (!ai || !ai->GetBot() || !quest || !questGiver)
        return false;

    // A local instance: this path only reads ai/bot/chat, none of which the engine
    // owns, and it is never registered or run as a normal tick.
    TalkToQuestGiverAction action(ai);
    std::string out;
    action.TurnInQuest(ai->GetMaster(), quest, questGiver, out, true);

    return ai->GetBot()->GetQuestRewardStatus(quest->GetQuestId());
}

bool TalkToQuestGiverAction::TurnInQuest(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out, bool autoHandIn)
{
    uint32 questID = quest->GetQuestId();
    if (bot->GetQuestRewardStatus(questID))
    {
        return false;
    }

    if (sPlayerbotAIConfig.globalSoundEffects)
    {
        bot->PlayDistanceSound(621);
    }

    // An auto hand-in is not a talk to the giver: the caller (the stuck-hand-in
    // fallback in MoveToTravelTargetAction) writes QuestAutoHandIn instead, so a
    // "talked to the quest giver" count keeps meaning exactly that.
    if (!autoHandIn)
        sPlayerbotAIConfig.logEvent(ai, "TalkToQuestGiverAction", quest->GetTitle(), std::to_string(quest->GetQuestId()));

    if (quest->GetRewChoiceItemsCount() == 0)
    {
        RewardNoItem(quest, questGiver, out);
    }
    else if (quest->GetRewChoiceItemsCount() == 1)
    {
        RewardSingleItem(quest, questGiver, out);
    }
    else
    {
        RewardMultipleItem(requester, quest, questGiver, out);
    }

    if(quest->GetRewChoiceItemsCount() || quest->GetRewItemsCount())
        ai->DoSpecificAction("equip upgrades");

    return true;
}

void TalkToQuestGiverAction::RewardNoItem(Quest const* quest, WorldObject* questGiver, std::string& out)
{
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    if (bot->CanRewardQuest(quest, false))
    {
        bot->RewardQuest(quest, 0, questGiver, false);
        sPlayerbotAIConfig.logEvent(ai, "QuestRewarded", quest->GetTitle(), std::to_string(quest->GetQuestId()));
        out = BOT_TEXT2("quest_status_completed", args);

        BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
    }
    else
    {
        out = BOT_TEXT2("quest_status_unable_to_complete", args);
    }
}

void TalkToQuestGiverAction::RewardSingleItem(Quest const* quest, WorldObject* questGiver, std::string& out)
{
    int index = 0;
    ItemPrototype const *item = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[index]);

    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);
    args["%item"] = chat->formatItem(item);

    if (bot->CanRewardQuest(quest, index, false))
    {
        bot->RewardQuest(quest, index, questGiver, true);
        sPlayerbotAIConfig.logEvent(ai, "QuestRewarded", quest->GetTitle(), std::to_string(quest->GetQuestId()));
        out = BOT_TEXT2("quest_status_complete_single_reward", args);

        BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
    }
    else
    {
        out = BOT_TEXT2("quest_status_unable_to_complete", args);
    }
}

ItemIds TalkToQuestGiverAction::BestRewards(Quest const* quest)
{
    ItemIds returnIds;
    ItemUsage bestUsage = ItemUsage::ITEM_USAGE_NONE;
    if (quest->GetRewChoiceItemsCount() == 0)
    {
        return returnIds;
    }
    else if (quest->GetRewChoiceItemsCount() == 1)
    {
        return { 0 };
    }
    else
    {
        uint32 guildShareRewardItemId = AI_VALUE(uint32, "guild share quest reward item");
        if (guildShareRewardItemId)
        {
            for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
            {
                if (quest->RewChoiceItemId[i] == guildShareRewardItemId)
                {
                    return { i };
                }
            }
        }

        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);
            if (usage == ItemUsage::ITEM_USAGE_EQUIP)
            {
                bestUsage = ItemUsage::ITEM_USAGE_EQUIP;
            }
            else if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP && bestUsage != ItemUsage::ITEM_USAGE_EQUIP)
            {
                bestUsage = usage;
            }
            else if (usage != ItemUsage::ITEM_USAGE_NONE && bestUsage == ItemUsage::ITEM_USAGE_NONE)
            {
                bestUsage = usage;
            }
        }
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);
            if (usage == bestUsage)
            {
                returnIds.insert(i);
            }
        }
        return returnIds;
    }
}

void TalkToQuestGiverAction::RewardMultipleItem(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out)
{
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    std::set<uint32> bestIds;
    std::ostringstream outid;
    auto questRewardOption = static_cast<QuestRewardOptionType>(AI_VALUE(uint8, "quest reward"));
    if (!ai->IsAlt() ||
        (questRewardOption == QuestRewardOptionType::QUEST_REWARD_CONFIG_DRIVEN && sPlayerbotAIConfig.autoPickReward == "yes") ||
        questRewardOption == QuestRewardOptionType::QUEST_REWARD_OPTION_AUTO
    ) {
        //Pick the best-scored item of the best rewards (AG-3/RPG-A1): usage
        // ties are broken by live stat weight instead of vendor order.
        bestIds = BestRewards(quest);
        uint32 rewardIndex = *bestIds.begin();
        uint32 rewardScore = 0;
        if (bestIds.size() > 1)
        {
            std::vector<std::pair<uint32, uint32>> scored;
            for (uint32 id : bestIds)
                scored.emplace_back(id, sRandomItemMgr.GetLiveStatWeight(bot, quest->RewChoiceItemId[id]));
            rewardIndex = ai::QuestRewardTiebreak(scored);
            for (auto const& candidate : scored)
                if (candidate.first == rewardIndex)
                    rewardScore = candidate.second;
        }
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[rewardIndex]);
        if (proto)
        {
            args["%item"] = chat->formatItem(proto);
            out = BOT_TEXT2("quest_status_complete_single_reward", args);

            BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
        }

        bot->RewardQuest(quest, rewardIndex, questGiver, true);
        sPlayerbotAIConfig.logEvent(ai, "QuestRewarded", quest->GetTitle(),
            std::to_string(quest->GetQuestId()) + (bestIds.size() > 1 ? " score " + std::to_string(rewardScore) : ""));
    }
    else if ((questRewardOption == QuestRewardOptionType::QUEST_REWARD_CONFIG_DRIVEN && sPlayerbotAIConfig.autoPickReward == "no") ||
             questRewardOption == QuestRewardOptionType::QUEST_REWARD_OPTION_LIST
    ) {
        // Old functionality, list rewards.
        AskToSelectReward(requester, quest, out, false);
    }
    else
    {
        // Try to pick the usable item. If multiple, list usable rewards.
        bestIds = BestRewards(quest);
        if (bestIds.size() > 1)
        {
            AskToSelectReward(requester, quest, out, true);
        }
        else
        {
            uint32 rewardIndex = bestIds.empty() ? 0 : *bestIds.begin();
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[rewardIndex]);
            if (proto)
            {
                args["%item"] = chat->formatItem(proto);
                out = BOT_TEXT2("quest_status_complete_single_reward", args);

                BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
            }

            bot->RewardQuest(quest, rewardIndex, questGiver, true);
            sPlayerbotAIConfig.logEvent(ai, "QuestRewarded", quest->GetTitle(), std::to_string(quest->GetQuestId()));
        }
    }
}

void TalkToQuestGiverAction::AskToSelectReward(Player* requester, Quest const* quest, std::string& out, bool forEquip)
{
    std::ostringstream msg;
    for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
    {
        ItemPrototype const* item = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[i]);
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);

        if (!forEquip || BestRewards(quest).count(i) > 0)
        {
            msg << "\n" << chat->formatItem(item);
        }
    }

    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);
    args["%rewards"] = msg.str();

    out = BOT_TEXT2("quest_status_complete_pick_reward", args);
}