
#include "QuestRewardActions.h"
#include "playerbot/strategy/values/QuestValues.h"

using namespace ai;

bool QuestRewardAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (event.GetParam() == "auto") {
        SET_AI_VALUE(uint8, "quest reward", static_cast<uint8>(QuestRewardOptionType::QUEST_REWARD_OPTION_AUTO));
    }
    else if (event.GetParam() == "list") {
        SET_AI_VALUE(uint8, "quest reward", static_cast<uint8>(QuestRewardOptionType::QUEST_REWARD_OPTION_LIST));
    }
    else if (event.GetParam() == "ask") {
        SET_AI_VALUE(uint8, "quest reward", static_cast<uint8>(QuestRewardOptionType::QUEST_REWARD_OPTION_ASK));
    }
    else if (event.GetParam() == "reset") {
        SET_AI_VALUE(uint8, "quest reward", static_cast<uint8>(QuestRewardOptionType::QUEST_REWARD_CONFIG_DRIVEN));
    }
    else if (event.GetParam() == "?") {
        auto currentQuestRewardOption = static_cast<QuestRewardOptionType>(AI_VALUE(uint8, "quest reward"));
        std::ostringstream out;
        out << "Current: |cff00ff00";
        switch (currentQuestRewardOption) {
        case QuestRewardOptionType::QUEST_REWARD_OPTION_AUTO:
            out << "Auto";
            break;
        case QuestRewardOptionType::QUEST_REWARD_OPTION_LIST:
            out << "List";
            break;
        case QuestRewardOptionType::QUEST_REWARD_OPTION_ASK:
            out << "Ask";
            break;
        default:
            out << "Config Driven";
            break;
        }
        ai->TellPlayer(requester, out);
    }
    else {
        ai->TellPlayer(requester, "Usage: quest reward [auto|list|ask|reset|?]");
    }

    return true;
}