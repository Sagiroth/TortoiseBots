
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "TravelAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "playerbot/QuestStallPolicy.h"
#include "playerbot/TravelMgr.h"


using namespace ai;
using namespace MaNGOS;

bool TravelAction::Execute(Event& event)
{
    TravelTarget * target = AI_VALUE(TravelTarget *, "travel target");

    target->CheckStatus();

    SET_AI_VALUE2(time_t, "manual time", "next travel check", time(0) + 5);

    // POI-stall abandon, arrival side (issue #423): the donor's 5-min
    // no-progress verdict, adapted to our objective travel. The horizon
    // starts at ARRIVAL (status WORK, inside the destination radius) - travel
    // time never counts. While WORK on a counter-tracked objective the
    // quest's kill/item counters are anchored per quest; a WORK tick with
    // unchanged counters past the horizon parks the quest's objective fetch
    // and nulls the target so the next search moves on. Any progress (or a
    // finished/failed quest) re-anchors/clears instead. Givers/takers
    // unaffected: parked quests still hand in. Pool upkeep bots only.
    if (sPlayerbotAIConfig.botQuestLogUpkeep && !ai->HasActivePlayerMaster() &&
        sRandomBotFacade.IsRandomBot(bot) &&
        target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
    {
        if (QuestObjectiveTravelDestination* dest =
            dynamic_cast<QuestObjectiveTravelDestination*>(target->GetDestination()))
        {
            uint32 const questId = dest->GetQuestId();
            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            QuestStatusMap& statuses = bot->getQuestStatusMap();
            auto statusIt = statuses.find(questId);
            std::string const anchorKey = "quest objective stall::" + std::to_string(questId);
            bool const inLog = quest && statusIt != statuses.end() &&
                !statusIt->second.m_rewarded &&
                statusIt->second.m_status == QUEST_STATUS_INCOMPLETE;
            // Exploration / event / spell objectives carry no kill or item
            // requirement and complete by other means - exempt: nothing to
            // count, so the verdict could never clear them fairly.
            bool trackable = false;
            std::array<std::uint32_t, QUEST_OBJECTIVES_COUNT> curKill = {};
            std::array<std::uint32_t, QUEST_OBJECTIVES_COUNT> curItem = {};
            if (inLog)
            {
                for (uint32 o = 0; o < QUEST_OBJECTIVES_COUNT; ++o)
                {
                    curKill[o] = statusIt->second.m_creatureOrGOcount[o];
                    curItem[o] = statusIt->second.m_itemcount[o];
                    trackable = trackable || ai::QuestObjectiveTrackable(
                        quest->ReqCreatureOrGOId[o], quest->ReqCreatureOrGOCount[o],
                        quest->ReqItemId[o], quest->ReqItemCount[o]);
                }
            }
            if (!inLog || !trackable)
            {
                sRandomBotFacade.SetValue(bot, anchorKey, 0, "", kQuestStallAnchorTtlSec);
            }
            else
            {
                QuestStallAnchor anchor;
                bool const hasAnchor = ParseQuestStallAnchor(
                    sRandomBotFacade.GetData(bot->GetGUIDLow(), anchorKey), anchor);
                if (!hasAnchor || curKill != anchor.kill || curItem != anchor.item)
                {
                    sRandomBotFacade.SetValue(bot, anchorKey, 1,
                        FormatQuestStallAnchor(time(0), curKill, curItem),
                        kQuestStallAnchorTtlSec);
                }
                else if (QuestObjectiveStalled(curKill, curItem, anchor.kill, anchor.item,
                    anchor.time, time(0)))
                {
                    SET_AI_VALUE2(time_t, "manual time",
                        "no quest objective until::" + std::to_string(questId),
                        time(0) + kQuestStallParkSec);
                    sPlayerbotAIConfig.logEvent(ai, "QuestObjectiveStalled",
                        quest->GetTitle(), std::to_string(questId));
                    sTravelMgr.SetNullTravelTarget(target);
                    RESET_AI_VALUE(bool, "travel target active");
                    return true;
                }
            }
        }
    }

    return false;
}

bool TravelAction::isUseful()
{
    if (!AI_VALUE(bool,"travel target active"))
        return false;

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->getObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            return false;

    if (sServerFacade.isMoving(bot))
        return false;

    if (AI_VALUE2(time_t, "manual time", "next travel check") > time(0))
        return false;

    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        return true;

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_COOLDOWN)
        return true;

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY && !urand(0, 20))
        return true;

    return false;
}
