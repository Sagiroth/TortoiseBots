
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "TravelAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "playerbot/QuestGiverStallPolicy.h"
#include "playerbot/QuestStallPolicy.h"
#include <cstdlib>
#include "AcceptQuestAction.h"
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
    // Giver-stall release, arrival side: the donor invalidates a questgiver
    // purpose whose arrival yields nothing (validity gates flip false once
    // the errand is done), while ours holds WORK for the full 5-min expiry
    // with the stay-alive conditions still green. A giver whose menu holds
    // no rewardable hand-in and no acceptable quest is done: expire the
    // target so the next tick re-picks instead of idling at the NPC. Same
    // OffersAcceptableQuest the nearby-service rule uses, so the two cannot
    // drift. Givers only: takers settle through their own hand-in path.
    // Pool upkeep bots only.
    if (sPlayerbotAIConfig.botQuestLogUpkeep && !ai->HasActivePlayerMaster() &&
        sRandomBotFacade.IsRandomBot(bot) &&
        target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
    {
        if (QuestRelationTravelDestination* giver =
            dynamic_cast<QuestRelationTravelDestination*>(target->GetDestination()))
        {
            if (giver->GetPurpose() == TravelDestinationPurpose::QuestGiver &&
                giver->GetEntry() > 0)
            {
                Creature* giverNpc = bot->FindNearestCreature(
                    (uint32)giver->GetEntry(), INTERACTION_DISTANCE * 2);
                if (giverNpc && giverNpc->IsAlive() &&
                    bot->IsWithinDistInMap(giverNpc, INTERACTION_DISTANCE * 2) &&
                    !AcceptAllQuestsAction::OffersAcceptableQuest(ai, bot, giverNpc))
                {
                    sPlayerbotAIConfig.logEvent(ai, "QuestGiverStalled",
                        giver->GetTitle(), std::to_string(giver->GetQuestId()));
                    // Giver-stall back-off: the pick gate reads static template
                    // fit while this stall reads the live menu (chain
                    // prerequisites, taken status, accept policy), so a giver
                    // whose menu never offers the quest is re-picked forever.
                    // The 2nd stall for one (giver, quest) without the quest
                    // state changing parks that pair 30 min, so the next
                    // search goes elsewhere. Pool upkeep bots only.
                    {
                        int32 const giverEntry = giver->GetEntry();
                        uint32 const questId = giver->GetQuestId();
                        std::string const stallKey = ai::QuestGiverStallKey(giverEntry, questId);
                        QuestStatus status = bot->GetQuestStatus(questId);
                        bool rewarded = bot->GetQuestRewardStatus(questId);
                        // Fingerprint: status plus rewarded (a hand-in flips
                        // rewarded without touching status; a drop resets both).
                        uint32 const curState = (uint32)status * 2 + (rewarded ? 1 : 0);
                        std::string const storedState = sRandomBotFacade.GetData(bot->GetGUIDLow(), stallKey);
                        bool const hasStored = !storedState.empty();
                        uint32 const prevState = hasStored ? (uint32)std::strtoul(storedState.c_str(), nullptr, 10) : 0;
                        uint32 stalls = 1;
                        if (!ai::QuestGiverStallStateChanged(prevState, hasStored, curState))
                            stalls = sRandomBotFacade.GetValue(bot, stallKey) + 1;
                        // Counter TTL 35 min: bridges the ~90 s stall loop but
                        // cannot outlive the 30-min park it arms (a stale
                        // counter re-firing right after expiry would re-park
                        // on one stall instead of two).
                        sRandomBotFacade.SetValue(bot, stallKey, stalls, std::to_string(curState),
                            (int32)(ai::kQuestGiverBackoffParkSec + 5 * 60));
                        if (ai::QuestGiverStallBacksOff(stalls))
                        {
                            SET_AI_VALUE2(time_t, "manual time", ai::QuestGiverBackoffKey(giverEntry, questId),
                                time(0) + ai::kQuestGiverBackoffParkSec);
                            sPlayerbotAIConfig.logEvent(ai, "QuestGiverBackoff",
                                giver->GetTitle(), std::to_string(questId));
                            sRandomBotFacade.SetValue(bot, stallKey, 0, std::to_string(curState),
                                (int32)(ai::kQuestGiverBackoffParkSec + 5 * 60));
                        }
                    }
                    target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
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
