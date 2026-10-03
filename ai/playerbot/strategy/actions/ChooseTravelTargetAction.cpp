
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/LootObjectStack.h"
#include "ChooseTravelTargetAction.h"
#include "FishAction.h"
#include "MoveToTravelTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PullRegenPolicy.h"
#include "playerbot/GrindSpotPolicy.h"
#include "playerbot/TravelInstancePolicy.h"
#include "playerbot/TravelRepickPolicy.h"
#include "playerbot/strategy/values/VendorTripPolicy.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/values/MaintenanceValues.h"
#include "playerbot/TravelNode.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/RandomBotFacade.h"
#include "Guild/GuildMgr.h"
#include <iomanip>
#include <mutex>

using namespace ai;

inline std::string GetTravelPurposeName(std::string purpose)
{
    if (Qualified::isValidNumberString(purpose) && TravelDestinationPurposeName.find(TravelDestinationPurpose(stoi(purpose))) != TravelDestinationPurposeName.end())
        return TravelDestinationPurposeName.at(TravelDestinationPurpose(stoi(purpose)));

    if (purpose.empty())
        return "quest";

    return purpose;
}

bool ai::TravelBlockedInsideInstance(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Map* map = bot->GetMap();
    bool const inInstance = map && (map->IsDungeon() || map->IsRaid());

    // Map check first: the master lookup walks the object accessor, and this
    // guard runs for every travel action on every tick in the open world too.
    if (!inInstance)
        return false;

    return TravelSelectionBlockedByInstance(ai->HasRealPlayerMaster(), inInstance);
}

bool ChooseTravelTargetAction::Execute(Event& event)
{
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if(travelTarget->GetStatus() != TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    Player* requester = event.GetOwner() ? event.GetOwner() : (GetMaster() ? GetMaster() : bot);
    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");
    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);
    uint32 targetRelevance = AI_VALUE2(int, "manual int", "future travel relevance");

    if (!futureDestinations->valid())
    {
        // The async search produced no usable result, so there is nothing to
        // choose from. Park this purpose the way the empty-search path
        // below does (same keys the request gate reads), so the bot does
        // not re-request - and re-search - on the very next tick. One
        // minute: the search offered nothing at all, which usually clears
        // fast (a destination briefly inactive, a stale partition). Key and
        // duration live in TravelRepickPolicy.h. Status first, then re-arm
        // only this purpose: other purposes' parks survive (timestamps stay
        // authoritative, flags stay set).
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
        std::string const invalidParkKey = TravelInvalidParkKey(futureTravelPurpose);
        SET_AI_VALUE2(bool, "no active travel destinations", invalidParkKey, true);
        SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + invalidParkKey,
            time(0) + TRAVEL_FUTURE_INVALID_PARK_SECONDS);
        return false;
    }

    if (futureDestinations->wait_for(std::chrono::seconds(0)) == std::future_status::timeout)
        return false;

    PartitionedTravelList destinationList = futureDestinations->get();

    travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);

    // Spread the grind: a creature spot already worked by as many bots as it has
    // room for drops out of this pick, and the bot takes the next candidate range
    // instead (see TravelMgr::DropCrowdedGrindPoints). Cheap - one hash lookup per
    // candidate, demand kept incrementally by TravelTarget, no scan over bots.
    if (futureTravelPurpose == std::to_string((uint32)TravelDestinationPurpose::Grind))
        sTravelMgr.DropCrowdedGrindPoints(destinationList);

    ai->TellDebug(ai->GetMaster(), "Got " + std::to_string(destinationList.size()) + " new destination ranges for " + futureTravelPurposeName, "debug travel");

    TravelTarget newTarget = TravelTarget(ai);

    if (futureTravelPurpose == "pvp")
        newTarget.SetForced(true);

    if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild meeting")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 199u));
    }
    else if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild order")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 198u));
    }
    else
    {
        newTarget.SetRelevance(targetRelevance);
    }

    if (!SetBestTarget(requester, &newTarget, destinationList))
    {
        // Park this purpose (flag + timestamp; other purposes' parks are left
        // alone). Without a park the purpose re-requested (and re-searched)
        // on the very next tick. A minute suits a
        // service errand, whose destination is usually inactive for a moment.
        // A quest search that came back empty is a different animal: every
        // gate that rejected it (quest level window, free log slots, area band,
        // route, reachability) is still true a minute later, and the search is a
        // full walk of the quest destinations. The plain quest row sits at 6.3,
        // live in the 15 minutes of each bot-hour where Grind's purpose value is
        // off, and the 6.36 hand-in row outranks Grind outright - so a one-minute
        // park put the bot back into a fruitless search every minute, aborting
        // its errand each time. Ten minutes is the trainer park's and the
        // QuestTripNoTarget log throttle's order of magnitude; the value that
        // ranks the hand-in row (has rewardable finished quest) reads this
        // timestamp, so the row stands down for the whole park.
        std::string const purposeKey = futureTravelPurpose.empty() ? "quest" : futureTravelPurpose;
        bool const questErrand = purposeKey == "quest";
        // A beginner pool bot that found nothing is not waiting out a real
        // backlog: below level 5 the whole quest search fits inside 1500 yd, so an
        // empty result only says "nothing in range right now", and the ten-minute
        // park would strand it at an NPC with grind as its only other errand
        // (issue #393). Retry next minute like any other purpose - unless it
        // carries a finished quest it could hand in, whose 6.36 row outranks
        // grind: re-searching that every minute would starve the very fallback
        // this shortens the park for.
        bool const beginnerRepark = questErrand && bot->GetLevel() < 5 &&
            sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster() &&
            !HasRewardableFinishedQuest(ai);
        SET_AI_VALUE2(bool, "no active travel destinations", purposeKey, true);
        SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey,
            time(0) + ((questErrand && !beginnerRepark) ? 10 * MINUTE : MINUTE));
        ai->TellDebug(ai->GetMaster(), "No target set", "debug travel");

        // TEMPORARY, see the probe in RequestQuestTravelTargetAction. Destinations
        // came back and none of them was accepted - worth telling apart from "none
        // were offered", which looks identical from the outside.
        if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
            sLog.outBasic("QUESTPROBE: %s got %u destination ranges for '%s' and picked none",
                bot->GetName(), uint32(destinationList.size()), futureTravelPurpose.c_str());

        //A vendor errand has no other diagnostic: a search that never yields a
        //destination leaves the loot in the bags and produces no event at all,
        //which is exactly how the pool reached 0 SellAction rows in 90 minutes
        //with no way to tell "the request never ran" from "it ran and found
        //nothing" (the trainer path has TrainerNoMoney for the same reason). One
        //line per ten minutes per bot; a vendor trip that works logs nothing.
        if (purposeKey == std::to_string((uint32)TravelDestinationPurpose::Vendor) &&
            AI_VALUE2(time_t, "manual time", "vendor trip no target log") <= time(0))
        {
            SET_AI_VALUE2(time_t, "manual time", "vendor trip no target log", time(0) + 10 * MINUTE);
            sPlayerbotAIConfig.logEvent(ai, "VendorTripNoTarget",
                std::to_string(destinationList.size()), std::to_string(bot->GetLevel()));
        }

        //And for the quest errand. A bot carrying a rewardable finished quest now
        //outranks Grind (see TravelStrategy), so this search runs often; the only
        //other outcome it can record is the success (QuestTravelToGiver/Taker), so
        //without this line "the request never ran" and "destinations came back and
        //none was accepted" look identical - the exact blind spot that hid the
        //6.3-vs-6.35 ranking bug for two realms. One line per ten minutes per bot.
        if (purposeKey == "quest" &&
            AI_VALUE2(time_t, "manual time", "quest trip no target log") <= time(0))
        {
            SET_AI_VALUE2(time_t, "manual time", "quest trip no target log", time(0) + 10 * MINUTE);
            sPlayerbotAIConfig.logEvent(ai, "QuestTripNoTarget",
                std::to_string(destinationList.size()), std::to_string(bot->GetLevel()));
        }

        return false;
    }

    setNewTarget(requester, &newTarget, travelTarget);

    return true;
}

bool ChooseTravelTargetAction::isUseful()
{
    // Only a prepared request has destinations to choose from; otherwise the
    // "not travel target active" trigger would run this every tick just to fail.
    return CanChooseTravel() &&
        AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE;
}

bool ChooseTravelTargetAction::CanChooseTravel()
{
    if (TravelBlockedInsideInstance(ai))
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (AI_VALUE(bool, "travel target active"))
        return false;

    return true;
}

void ChooseTravelTargetAction::setNewTarget(Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    if (CanFreeMoveValue::CanFreeMoveTo(ai, newTarget->GetPosStr()))
        ReportTravelTarget(bot, requester, newTarget, oldTarget);

    //If we are heading to a creature/npc clear it from the ignore list.
    if (oldTarget && oldTarget == newTarget && newTarget->GetEntry())
    {
        std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();

        for (auto& i : ignoreList)
        {
            if (i.GetEntry() == newTarget->GetEntry())
            {
                ignoreList.erase(i);
            }
        }

        context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Set(ignoreList);
    }

    //Actually apply the new target to the travel target used by the bot.
    oldTarget->CopyTarget(newTarget);

    if (oldTarget->IsForced()) //Make sure travel goes into cooldown after getting to the destination.
        oldTarget->SetExpireIn(HOUR * IN_MILLISECONDS);

    if(!AI_VALUE2(std::string, "manual string", "future travel condition").empty())
        AI_VALUE(TravelTarget*, "travel target")->SetConditions({ AI_VALUE2(std::string, "manual string", "future travel condition")});

    if (QuestObjectiveTravelDestination* dest = dynamic_cast<QuestObjectiveTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition = "group or::{following party,need quest objective::{" + std::to_string(dest->GetQuestId()) + "," + std::to_string((uint8)dest->getObjective()) + "}}";
        oldTarget->AddCondition(condition);
        if (Quest const* q = sObjectMgr.GetQuestTemplate(dest->GetQuestId()))
            sPlayerbotAIConfig.logEvent(ai, "QuestTravelToObjective", q->GetTitle(), std::to_string(dest->GetQuestId()));
    }
    else if (QuestRelationTravelDestination* dest = dynamic_cast<QuestRelationTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition, qualifier = std::to_string(dest->GetEntry());
        if (dest->GetPurpose() == TravelDestinationPurpose::QuestGiver)

            condition = "group or::{following party,or::{can accept quest npc::" + qualifier + ",can accept quest low level npc::" + qualifier + "}}";
        else
            condition = "group or::{following party,can turn in quest npc::" + qualifier + "}";

        oldTarget->AddCondition(condition);
        if (Quest const* q = sObjectMgr.GetQuestTemplate(dest->GetQuestId()))
        {
            std::string eventName = (dest->GetPurpose() == TravelDestinationPurpose::QuestGiver) ? "QuestTravelToGiver" : "QuestTravelToTaker";
            sPlayerbotAIConfig.logEvent(ai, eventName, q->GetTitle(), std::to_string(dest->GetQuestId()));
        }

        // Picking the same hand-in taker again while still stuck near it (the
        // taker within range but out of interaction distance) is one no-progress
        // episode for that quest: the trip that produced this pick made none.
        // MoveToTravelTargetAction settles the hand-in off these episodes when
        // its own navmesh probe cannot.
        if (dest->GetPurpose() == TravelDestinationPurpose::QuestTaker)
            MoveToTravelTargetAction::CountHandInNoProgress(ai, dest->GetQuestId(), dest->GetEntry());
    }

    // Travel-target observability: one line per newly chosen target. A pick from
    // a null target (after a drop, a ding, a stuck retire) used to log nothing,
    // so the AH funnel was undercounted: 14 AH drops but only 6 logged AH picks
    // (issue #399 side note). As coded before, the old target was read AFTER
    // CopyTarget above, so the check could never fire - while the live binary
    // logged the old null zone (Alterac Mountains, the null destination at
    // map 0 (0,0,0)) with the marker. Now the zone is the NEW destination's
    // and the from-null state is the OLD target's pre-copy state. Resets
    // (ResetTargetAction) pass a fresh null target through here with a stale
    // purpose value, so the skip reads the NEW side, not the old one - every
    // real pick is non-null there and still logs. logEvent no-ops unless
    // bot_events.csv is in AllowedLogFiles. Predicates live in
    // TravelRepickPolicy.h.
    TravelDestination* oldPickDest = oldTarget ? oldTarget->GetDestination() : nullptr;
    bool const pickedFromNull = oldTarget != newTarget &&
        TravelTargetIsNull(oldPickDest != nullptr,
            oldPickDest != nullptr && typeid(*oldPickDest) == typeid(NullTravelDestination));
    TravelDestination* newPickDest = newTarget ? newTarget->GetDestination() : nullptr;
    bool const newPickIsNull = TravelTargetIsNull(newPickDest != nullptr,
        newPickDest != nullptr && typeid(*newPickDest) == typeid(NullTravelDestination));
    std::string const travelPurposeName = GetTravelPurposeName(AI_VALUE2(std::string, "manual string", "future travel purpose"));
    bool const isResetToNull = TravelIsResetToNull(pickedFromNull, newPickIsNull, travelPurposeName);
    if (!isResetToNull)
    {
        std::string purpose = travelPurposeName;
        WorldPosition* newPickPos = newTarget ? newTarget->getPosition() : nullptr;
        std::string destZone = (newPickPos && newPickPos->GetArea())
            ? newPickPos->GetAreaName(true, true) : "";
        if (pickedFromNull)
            destZone += " (from null)";
        sPlayerbotAIConfig.logEvent(ai, "TravelTarget", purpose, destZone);

        // One vendor journey at a time: a walk to a vendor has just started, and
        // the request gate refuses another until this is ten minutes old, a sale
        // has landed (SellAction) or the bot has dinged (XpGainAction,
        // AutoLearnSpellAction). Without it a trip the bot never walks - move
        // starved by loot and attacks, or a target wiped by a stuck reset - was
        // re-issued as fast as the request gate re-armed: 3,113 vendor picks in
        // 2 h 46 min, median re-pick gap 64 s, 44 % of gaps under 30 s, against
        // ~170 completed trips a day earlier (issue #399). Request-side only,
        // like the trainer window below: the travel trigger doubles as the
        // in-flight trip's stored condition, so gating the trigger would drop
        // the trip itself on the next travel check. See VendorTripPolicy.h and
        // RequestTravelTargetAction::isUseful.
        if (purpose == "Vendor")
            SET_AI_VALUE2(time_t, "manual time", "vendor trip since", time(0));
        // One trainer journey at a time: a walk to a trainer has just started, and
        // the errand's trigger refuses another until this is ten minutes old, the
        // bot has learned something (TrainerAction) or it has dinged
        // (AutoLearnSpellAction). Without it a trip that never reached the trainer
        // was re-issued as fast as its travel target died - cycle-3 pool, level 5:
        // 1,124 trainer-class picks from 172 bots in 90 min, 638 of 952 consecutive
        // picks made from the same coordinate, against ~130 actual learns. See
        // ShouldTravelNamedValue.
        if (purpose.find("trainer") == 0)
            SET_AI_VALUE2(time_t, "manual time", "trainer trip since", time(0));
    }

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);

    // A genuinely new destination ends any stuck-keep streak (see UnstuckAction):
    // the streak counts consecutive resets without progress toward one spot,
    // so a fresh pick starts it over instead of inheriting a retirement.
    RESET_AI_VALUE2(int32, "manual int", "stuck keep count");

    //Clear rpg and attack/grind target. We want to travel, not hang around some more.
    RESET_AI_VALUE(GuidPosition,"rpg target");
    RESET_AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target");
    RESET_AI_VALUE(ObjectGuid,"attack target");
    RESET_AI_VALUE(ObjectGuid,"explicit attack target");
    RESET_AI_VALUE(bool, "travel target active");
    // No blanket ClearValues: a pick of this purpose must not wipe other
    // purposes' time-boxed parks (timestamps stay authoritative, flags are
    // cleared lazily by the request gate once their park expires).
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());
};

//Tell the master what travel target we are moving towards.
//This should at some point be rewritten to be denser or perhaps logic moved to ->GetTitle()
void ChooseTravelTargetAction::ReportTravelTarget(Player* bot, Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    TravelDestination* destination = newTarget->GetDestination();

    TravelDestination* oldDestination;

    if (oldTarget)
        oldDestination = oldTarget->GetDestination();

    std::ostringstream out;

    if (newTarget->IsForced())
        out << "(Forced) ";

    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);

    std::string futureTravelCondition = AI_VALUE2(std::string, "manual string", "future travel condition");
    bool isGuildMeeting = futureTravelCondition == "should travel named::guild meeting";

    std::string futureTravelDetail = AI_VALUE2(std::string, "manual string", "future travel detail");

    std::string shortName = destination->GetShortName();

    if (typeid(*destination) == typeid(NullTravelDestination))
    {
        out.clear();
        if (!oldDestination || typeid(*oldDestination) != typeid(NullTravelDestination))
            out << "Nowhere to travel. Idling a bit.";
    }
    else
    {
        if (newTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        {
            out << "Currently";

            if (newTarget->getPosition() && !newTarget->getPosition()->GetAreaName().empty())
            {
                if (destination->DistanceTo(bot) < 100.0f)
                    out << " in ";
                else
                    out << " near ";

                out << newTarget->getPosition()->GetAreaName();
            }
            else
                out << " traveling";
        }
        else
        {
            if (bot->GetGroup() && !ai->IsGroupLeader() && (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT)))
                out << "I want to travel";
            else if (newTarget->IsGroupCopy() && newTarget->GetGroupmember().GetPlayer())
                out << "Taking " << newTarget->GetGroupmember().GetPlayer()->GetName();
            else if (oldDestination && oldDestination == destination)
                out << "Continuing";
            else
                out << "Traveling";

            if (newTarget->getPosition())
            {
                out << " " << round(newTarget->Distance(bot)) << "y";
                if (!newTarget->getPosition()->GetAreaName().empty())
                    out << " to " << newTarget->getPosition()->GetAreaName();
            }
        }

        if (shortName.find("quest") == 0)
        {
            QuestTravelDestination* QuestDestination = (QuestTravelDestination*)destination;
            out << " for " << QuestDestination->QuestTravelDestination::GetTitle();
            out << " to " << QuestDestination->GetTitle();
        }
        else if (shortName == "rpg")
        {
            out << " to " << destination->GetTitle();

            if (futureTravelPurpose == "city")
                out << " to hang around in the city";
            else if (futureTravelPurpose == "tabard")
                out << " to buy a tabard";
            else if (futureTravelPurpose == "petition")
                out << " to hand in a petition";
            else
                out << " to roleplay";
        }
        else
        {
            out << " to " << destination->GetTitle();
        }
    }

    if (newTarget->GetRetryCount(false))
        out << " (retry " << newTarget->GetRetryCount(false) << "/5)";
    if (out.str().empty())
        return;

    // Travel picks are autonomous AI, not command replies: when the requester
    // is another pool bot the TellPlayerNoFacing below reaches /say or party
    // chat. Keep the direct command reply to a live player; else stay silent
    // (debug and travel_map.csv logging below still run).
    if (!isGuildMeeting && requester && isRealPlayer_Helper(requester))
        ai->TellPlayerNoFacing(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_TALK, false);

    if (!futureTravelDetail.empty())
        ai->TellDebug(requester, "Farming item: " + futureTravelDetail + " from " + destination->GetTitle(), "debug travel");

    std::string message = out.str().c_str();

    if (sPlayerbotAIConfig.hasLog("travel_map.csv"))
    {
        WorldPosition botPos(bot);
        WorldPosition destPos = *newTarget->getPosition();

        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
        out << bot->GetName() << ",";
        out << std::fixed << std::setprecision(2);

        out << std::to_string(bot->GetRace()) << ",";
        out << std::to_string(bot->GetClass()) << ",";
        float subLevel = ai->GetLevelFloat();

        out << subLevel << ",";

        if (!destPos)
            destPos = botPos;

        botPos.printWKT({ botPos,destPos }, out, 1);

        if (typeid(*destination) == typeid(NullTravelDestination))
            out << "0,";
        else
            out << round(newTarget->GetDestination()->DistanceTo(botPos)) << ",";

        out << "new," << "\"" << destination->GetTitle() << "\",\"" << message << "\"";

        out << "," << futureTravelPurposeName;

        sPlayerbotAIConfig.log("travel_map.csv", out.str().c_str());
    }
}

inline std::string PrintPartion(uint32 sqPartition)
{
    uint32 prevPartition = 0;
    for (auto& partition : travelPartitions)
    {
        if (sqrt(sqPartition) == partition)
            return std::to_string(prevPartition) + "-" + std::to_string(partition);

        prevPartition = partition;
    }

    return "> " + std::to_string(prevPartition);
}

//Sets the target to the best destination.
// A destination whose walking route crosses a zone the bot cannot survive is no
// destination: the gates in SetBestTarget only look at the area of the target itself
// (a level-6 bot standing in Stormwind happily picks a Coldridge Valley quest and walks
// the Burning Steppes to get there). The route is the travel-node route the bot would
// follow; a route that does not exist at all (another continent without a transfer) is
// rejected too. Short hops on the same map are not checked - no A* for the everyday case.
static bool RouteIsSurvivableUncached(Player* bot, WorldPosition const& start, WorldPosition* position, std::string& blocker)
{
    std::vector<WorldPosition> beginPath, endPath;
    TravelNodeRoute route = sTravelNodeMap.getRoute(start, *position, beginPath, endPath, bot);
    if (route.isEmpty())
    {
        if (start.getMapId() != position->getMapId())
        {
            blocker = "no route to that continent";
            return false;
        }
        return true; // same map without a node route: the pathfinder handles it directly
    }
    int32 const limit = (int32)bot->GetLevel() + 5;
    bool ok = true;
    // Hostile-town route leg (cheap: only the named route nodes are scanned, no
    // extra spawn-table walk beyond the per-node guard check; start/end legs and
    // the 1000 yd short-hop bypass above are untouched). Random masterless bots
    // only; the check needs an ai for the master gate, so callers pass it in.
    PlayerbotAI* routeAi = PlayerbotAIStorage::Instance().GetAI(bot);
    bool const avoidTowns = sPlayerbotAIConfig.avoidHostileTowns && routeAi && !routeAi->HasRealPlayerMaster();
    for (TravelNode* node : route.getNodes())
    {
        WorldPosition* p = node ? node->getPosition() : nullptr;
        if (!p)
            continue;
        int32 const level = p->GetAreaLevel();
        if (level > 0 && level > limit)
        {
            blocker = p->getAreaName(true, true) + " (level " + std::to_string(level) + ")";
            ok = false;
            break;
        }
        if (avoidTowns && p->IsGuardedHostileTownFor(bot))
        {
            blocker = p->getAreaName(true, true) + " (hostile town guards)";
            ok = false;
            break;
        }
    }
    route.cleanTempNodes();
    return ok;
}

// The verdict is cached per bot, level, hostile-town mode, checked-hop mode,
// start area (200 yd cells) and destination (25 yd cells) for 5 minutes.
// SetBestTarget re-checks the same candidates
// on every pick, and a rejected or unreachable destination costs a full
// travel-node A* each time; with hundreds of bots that search dominated the
// world thread. Gold, reputation and graph changes are picked up when the
// entry expires.
// checkShortHop makes the 1000 yd bypass below not apply: callers use it for
// trips a bot below level 10 makes on foot, where the bypass would silently
// skip the one check that keeps a starter-zone hand-in out of a higher-level
// neighbourhood (see SetBestTarget).
static bool RouteIsSurvivable(Player* bot, WorldPosition* position, std::string& blocker, bool checkShortHop = false)
{
    WorldPosition start(bot);
    if (!checkShortHop && start.getMapId() == position->getMapId() && start.distance(*position) < 1000.0f)
        return true;

    struct Verdict
    {
        bool ok;
        std::string blocker;
        time_t expires;
    };
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, Verdict> cache;

    PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
    bool const avoidTowns = sPlayerbotAIConfig.avoidHostileTowns && botAi && !botAi->HasRealPlayerMaster();

    std::ostringstream key;
    key << bot->GetGUIDLow() << ':' << bot->GetLevel() << ':' << avoidTowns << ':' << checkShortHop << ':'
        << start.getMapId() << ':' << int32(std::floor(start.getX() / 200.0f)) << ':' << int32(std::floor(start.getY() / 200.0f)) << ':'
        << position->getMapId() << ':' << int32(std::floor(position->getX() / 25.0f)) << ':' << int32(std::floor(position->getY() / 25.0f));

    time_t const now = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(key.str());
        if (it != cache.end() && it->second.expires > now)
        {
            blocker = it->second.blocker;
            return it->second.ok;
        }
    }

    bool const ok = RouteIsSurvivableUncached(bot, start, position, blocker);

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cache.size() > 50000)
    {
        for (auto it = cache.begin(); it != cache.end();)
            it = it->second.expires <= now ? cache.erase(it) : std::next(it);
        if (cache.size() > 50000)
            cache.clear();
    }
    cache[key.str()] = { ok, ok ? std::string() : blocker, now + 300 };
    return ok;
}

bool ChooseTravelTargetAction::SetBestTarget(Player* requester, TravelTarget* target, PartitionedTravelList& partitionedList, bool onlyActive)
{
    bool distanceCheck = true;
    std::unordered_map<TravelDestination*, bool> isActive;

    bool hasTarget = false;

    for (auto& [partition, travelPointList] : partitionedList)
    {
        ai->TellDebug(requester, "Found " + std::to_string(travelPointList.size()) + " points at range " + PrintPartion(partition), "debug travel");

        for (auto& [destination, position, distance] : travelPointList)
        {
            if (!target->IsForced() && isActive.find(destination) != isActive.end() && !isActive[destination])
                continue;

            if (distanceCheck) //Check if we have moved significantly after getting the destinations.
            {
                WorldPosition center(requester ? requester : bot);
                if (position->distance(center) > distance * 2 && position->distance(center) > 100)
                {
                    ai->TellDebug(requester, "We had some destinations but we moved too far since. Trying to get a new list.", "debug travel");
                    return false;
                }

                distanceCheck = false;
            }

            if (target->IsForced() || (isActive[destination] = destination->IsActive(bot, PlayerTravelInfo(bot))))
            {
                // Checked after IsActive so the area lookup only happens for
                // the point that was actually selected.
                if (!target->IsForced() && position)
                {
                    if (position->IsEnemyHomeZoneFor(bot->GetTeam()))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - enemy home zone", "debug travel");
                        continue;
                    }

                    // Hostile-town guard: destination sits among guards hostile to this
                    // bot's team (static spawn data, 60 yd). Random masterless bots
                    // only; owned/alt bots obey their player. Enemy home zones are
                    // already skipped above; this covers contested-zone towns
                    // (Splintertree, Booty Bay, Southshore, Menethil...).
                    if (sPlayerbotAIConfig.avoidHostileTowns && !ai->HasRealPlayerMaster() &&
                        position->IsGuardedHostileTownFor(bot))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - hostile town guards", "debug travel");
                        continue;
                    }

                    AreaTableEntry const* area = position->GetArea();
                    uint32 zoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
                    if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
                        (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || (area && PlayerbotAIConfig::IsIsolatedCustomZone(area->Id))))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - custom starting zone", "debug travel");
                        continue;
                    }

                    int32 posAreaLevel = position->GetAreaLevel();
                    // Starter-valley exemption for the grind pick (GrindSpotPolicy.h):
                    // the valley average sits far above a level 1-4 bot while its
                    // mobs are vetted in-cap by the destination band, so the
                    // ceiling would veto every local point. Mirrors the
                    // IsLocationLevelValid beginnerGrind exemption. Owned/hired
                    // bots keep the ceiling.
                    bool const valleyExempted = destination->GetPurpose() == TravelDestinationPurpose::Grind &&
                        ai::GrindValleyExempted(bot->GetLevel(),
                            sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster());
                    if (!valleyExempted && posAreaLevel > 0 && posAreaLevel > (int32)bot->GetLevel() + 5)
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - area level too high", "debug travel");
                        continue;
                    }

                    if (destination->GetPurpose() == TravelDestinationPurpose::GatherFishing && IsFishingSpotGuarded(bot, *position))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - fishing spot guarded by hostile creatures", "debug travel");
                        continue;
                    }

                    // Death-spot avoidance (issue #398) at pick time, per point:
                    // a point inside a camp the bot keeps dying in is refused
                    // and the next candidate wins instead, while other camps of
                    // the same grind creature or quest objective stay usable.
                    // Grind and quest objectives only - givers, takers and
                    // services stay walkable.
                    uint32 const pickPurposeId = (uint32)destination->GetPurpose();
                    bool const pickIsDeathGated = ai::IsDeathGatedPurpose(pickPurposeId,
                        (uint32)TravelDestinationPurpose::Grind, (uint32)TravelDestinationPurpose::QuestAllObjective);
                    if (pickIsDeathGated && ai->IsDeathSpotAvoided(position->GetMapId(), position->getX(),
                        position->getY(), WorldTimer::getMSTime()))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - death spot avoided", "debug travel");
                        continue;
                    }

                    if (bot->GetLevel() <= 5 && position->distance(bot) > 1500.0f)
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - too far for starting level", "debug travel");
                        continue;
                    }

                    std::string blocker;
                    // The 1000 yd bypass inside RouteIsSurvivable skips exactly
                    // the trips a beginner makes - starter-zone takers and
                    // breadcrumb reports sit a few hundred yards away - and a
                    // taker is the one destination the search layer does not
                    // level-check (IsLocationLevelValid exempts QuestTaker,
                    // because inside the async search the destination's area is
                    // frequently unresolved) while the band here is the loose
                    // +5. A masterless bot below level 10 therefore walks the
                    // route like a long journey, so a hand-in across a
                    // higher-level neighbourhood is refused instead of walked.
                    // One travel-node A*, cached five minutes per candidate.
                    bool const checkTakerRoute = !ai->HasRealPlayerMaster() && bot->GetLevel() < 10 &&
                        destination->GetPurpose() == TravelDestinationPurpose::QuestTaker;
                    if (!RouteIsSurvivable(bot, position, blocker, checkTakerRoute))
                    {
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - route crosses " + blocker, "debug travel");
                        if (sPlayerbotAIConfig.hasLog("travel_route_gate.csv"))
                        {
                            std::ostringstream out;
                            out << bot->GetName() << "," << bot->GetLevel() << "," << destination->GetTitle() << "," << blocker;
                            sPlayerbotAIConfig.log("travel_route_gate.csv", out.str().c_str());
                        }
                        continue;
                    }
                }

                if (partition != std::prev(partitionedList.end())->first && !urand(0, 10)) //10% chance to skip to a longer partition.
                {
                    ai->TellDebug(requester, "Skipping range " + PrintPartion(partition), "debug travel");
                    break;
                }


                target->SetTarget(destination, position);
                hasTarget = true;
                break;
            }
            else
            {
                ai->TellDebug(requester, "Not active: " + destination->GetTitle() + " " + std::to_string((uint32)round(destination->DistanceTo(bot))) + "y", "debug travel");
            }

        }

        if (hasTarget)
            break;
    }

    if(hasTarget)
        ai->TellDebug(requester, "Point at " + std::to_string(uint32(target->Distance(bot))) + "y selected.", "debug travel");

    return hasTarget;
}

std::vector<std::string> split(const std::string& s, char delim);
char* strstri(const char* haystack, const char* needle);

//Find a destination based on (part of) it's name. Includes zones, ncps and mobs. Picks the closest one that matches.
DestinationList ChooseTravelTargetAction::FindDestination(PlayerTravelInfo info, std::string name, bool zones, bool npcs, bool quests, bool mobs, bool bosses, bool gather)
{
    DestinationList dests;

    //Quests
    if (quests)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Zones
    if (zones)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Explore, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Npcs
    if (npcs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GenericRpg, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Mobs
    if (mobs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Grind, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Bosses
    if (bosses)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Boss, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Gather
    if (gather)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherMining, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }

        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherHerbalism, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    if (dests.empty())
        return {};

    return dests;
};

bool ChooseGroupTravelTargetAction::Execute(Event& event)
{
    std::vector<ObjectGuid> groupPlayers;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (Player* member : LiveGroupMembers(group))
    {
        if (member != bot)
        {
            groupPlayers.push_back(member->getObjectGuid());
        }
    }

    std::shuffle(groupPlayers.begin(), groupPlayers.end(), *GetRandomGenerator());

    PlayerTravelInfo info(bot);

    std::vector<TravelTarget*> groupTargets;

    PartitionedTravelList travelList;

    std::unordered_map<TravelDestination*, std::vector<std::string>> conditions;
    std::unordered_map<TravelDestination*, Player*> playerDesitnations;

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    //Find targets of the group.
    for (auto& member : groupPlayers)
    {
        Player* player = sObjectMgr.GetPlayer(member);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext())
            continue;

        TravelTarget* groupTarget = PAI_VALUE(TravelTarget*, "travel target");

        if (groupTarget->IsGroupCopy())
            continue;

        if (!groupTarget->IsActive())
            continue;

        if (groupTarget->IsForced())
            continue;

        if (!groupTarget->GetDestination()->IsActive(player, PlayerTravelInfo(player)) || !groupTarget->IsConditionsActive())
        {
            PlayerbotAIStorage::Instance().GetAI(player)->TellDebug(requester,"Target is cooling down because a group member found it to be inactive.", "debug travel");
            groupTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
            continue;
        }

        groupTargets.push_back(groupTarget);
        playerDesitnations[groupTarget->GetDestination()] = player;
        conditions[groupTarget->GetDestination()] = groupTarget->GetConditions();
    }

    std::sort(groupTargets.begin(), groupTargets.end(), [](TravelTarget* i, TravelTarget* j) {return i->GetRelevance() > j->GetRelevance(); });

    ai->TellDebug(requester, std::to_string(groupTargets.size()) + " group targets found.", "debug travel");

    for (auto& groupTarget : groupTargets)
    {
        travelList[0].push_back(TravelPoint(groupTarget->GetDestination(), groupTarget->getPosition(), groupTarget->getPosition()->distance(bot)));

        ai->TellDebug(requester, playerDesitnations[groupTarget->GetDestination()]->GetName() + std::string(": ") + groupTarget->GetDestination()->GetShortName() + std::string(" (") + std::to_string(groupTarget->GetRelevance()) + std::string(")"), "debug travel");
    }

    if (travelList[0].empty())
        return false;

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    TravelTarget newTarget = TravelTarget(ai);

    if (!SetBestTarget(requester, &newTarget, travelList))
        return false;

    newTarget.SetGroupCopy(playerDesitnations[newTarget.GetDestination()]);

    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetConditions(conditions[newTarget.GetDestination()]);

    return true;
}

bool ChooseGroupTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!bot->GetGroup())
        return false;

    if (!CanChooseTravel())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (urand(0, 100) < 50)
        return false;

    return true;
}

bool RefreshTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    TravelDestination* oldDestination = target->GetDestination();

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    if (target->IsMaxRetry(false))
    {
        ai->TellDebug(requester, "Old destination was tried too many times.", "debug travel");
        return false;
    }

    if (!oldDestination) //Does this target have a destination?
        return false;

    if (!target->IsDestinationActive()) //Is the destination still valid?
    {
        ai->TellDebug(requester, "Old destination was no longer valid.", "debug travel");
        return false;
    }

    // Death-spot avoidance (issue #398): a re-point of the same camp the bot
    // keeps dying in is refused so the refresh falls through to a fresh pick
    // elsewhere instead of re-arming the loop. Grind and quest objectives
    // only, same set as the gates above.
    WorldPosition* refreshPoint = target->getPosition();
    if (oldDestination && refreshPoint)
    {
        uint32 const refreshPurposeId = (uint32)oldDestination->GetPurpose();
        bool const refreshIsDeathGated = ai::IsDeathGatedPurpose(refreshPurposeId,
            (uint32)TravelDestinationPurpose::Grind, (uint32)TravelDestinationPurpose::QuestAllObjective);
        if (refreshIsDeathGated && ai->IsDeathSpotAvoided(refreshPoint->GetMapId(), refreshPoint->getX(),
            refreshPoint->getY(), WorldTimer::getMSTime()))
        {
            ai->TellDebug(requester, "Old destination is death-spot avoided.", "debug travel");
            return false;
        }
    }

    PlayerTravelInfo info(bot);

    WorldPosition* newPosition;

    for (uint8 i = 0; i < 5; i++)
    {
        std::list<uint8> chancesToGoFar = { 10,20,90 }; //Closest map, grid, cell.
        newPosition = oldDestination->GetNextPoint(*target->getPosition(), chancesToGoFar);
        if (newPosition && sTravelMgr.IsLocationLevelValid(*newPosition, info, (uint32)oldDestination->GetPurpose()))
            break;
    }

    if (!newPosition)
    {
        ai->TellDebug(requester, "No new locations found for old destination.", "debug travel");
        return false;
    }

    SET_AI_VALUE2(bool, "manual bool", "is travel refresh", true);
    bool conditionsStillActive = AI_VALUE(TravelTarget*, "travel target")->IsConditionsActive(true);
    RESET_AI_VALUE2(bool, "manual bool", "is travel refresh");

    if (!conditionsStillActive)
        return false;

    target->SetTarget(oldDestination, newPosition);

    target->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
    target->IncRetry(false);

    RESET_AI_VALUE(bool, "travel target active");
    // A re-point keeps the same destination: other purposes' parks are left
    // alone (see setNewTarget).
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());

    ai->TellDebug(requester, "Refreshed travel target", "debug travel");
    ReportTravelTarget(bot, requester, target, target);

    // A successful re-point did the work: report success. Returning false
    // here read as a failure and tripped ACTION_LOOP telemetry while the
    // destination stayed active.
    return true;
}

bool RefreshTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!CanChooseTravel())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (!WorldPosition(bot).isOverworld())
        return false;

    if (urand(1, 100) <= 10)
        return false;

    if (!AI_VALUE(TravelTarget*, "travel target")->GetDestination()->IsActive(bot, PlayerTravelInfo(bot)))
        return false;

    return true;
}

bool ResetTargetAction::Execute(Event& event)
{
    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    // A reset picks nothing, so other purposes' time-boxed parks are left
    // alone.
    TravelTarget newTarget = TravelTarget(ai);
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
    oldTarget->SetExpireIn(60000); //1 minute;

    ai->TellDebug(requester, "Cleared travel target fetches", "debug travel");

    return true;
}

bool ResetTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!CanChooseTravel())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    return true;
}

bool RequestTravelTargetAction::Execute(Event& event)
{
    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));

    WorldPosition center = event.GetOwner() ? event.GetOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for " + TravelDestinationPurposeName.at(actionPurpose), "debug travel");

    // Leave-rule Grind must land in a zone that fits the BOT: destination
    // area level + 5 >= bot level (same shape as the leave rule itself),
    // alongside the existing mob-level window. Zone-level first: a zone whose
    // own level is outgrown can still field top-tier mobs inside the mob
    // window, which re-picks the old zone forever. Ordinary Grind (floor 0)
    // keeps its wider window.
    bool const leavingOutgrown = event.GetSource() == "should leave outgrown zone";
    int32 outgrownFloor = 0;
    if (leavingOutgrown && actionPurpose == TravelDestinationPurpose::Grind)
        outgrownFloor = (int32)bot->GetLevel() - 5;

    // A vendor errand searches the way the named trainer request does - without
    // the RpgTravelDestination::IsPossible pre-filter. That filter is the only
    // search-side difference between the trainer errand (which works: 809 trainer
    // travel targets and 279 purchases in the same window) and the vendor errand
    // (0 Vendor targets), and the gates that actually protect the walk are not in
    // it: the partition gate still enforces the beginner radius
    // (IsLocationLevelValid, LowLevelVendorMaxDistance), the area-level fit and
    // the <=5 level distance cap, and SetBestTarget still applies the hostile
    // town, area-level, distance and route checks.
    bool const onlyPossible = actionPurpose != TravelDestinationPurpose::Vendor;

    *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, purpose = actionPurpose, outgrownFloor, onlyPossible]() { return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)purpose, {}, onlyPossible, 10000.0f, outgrownFloor); });

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition", event.GetSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    // Outgrown-zone observability: one line per actual request (not per value
    // tick). logEvent no-ops unless bot_events.csv is in AllowedLogFiles.
    // ValueTrigger::Check emits the bare value name (Trigger::Check builds the
    // event from getName(), which ValueTrigger sets to its qualifier), so the
    // source here is "should leave outgrown zone", never "val::...".
    if (event.GetSource() == "should leave outgrown zone")
    {
        std::string reason = WorldPosition(bot).HasAreaFlag(AREA_FLAG_CAPITAL) ? "capital" : "outgrown";
        sPlayerbotAIConfig.logEvent(ai, "LeaveOutgrownZone", WorldPosition(bot).GetAreaName(true, true), reason);
    }

    return true;
}

bool RequestTravelTargetAction::isUseful() {
    if (bot->InBattleGround())
        return false;

    if (TravelBlockedInsideInstance(ai))
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (AI_VALUE(bool, "travel target active") && !VendorErrandWhileParked(ai, getQualifier()))
        return false;

    // One vendor journey at a time (issue #399): a picked vendor trip
    // suppresses new vendor requests for ten minutes, until a sale lands or
    // the bot dings (both clear "vendor trip since"). Without it a trip the
    // bot never walks was re-requested as fast as the gate re-armed - 44 % of
    // vendor re-pick gaps under 30 s. Pure helper in VendorTripPolicy.h so the
    // rule is unit-tested; request-side only, so it cannot drop the trip the
    // travel trigger's stored condition keeps alive. Bag-pressure re-requests
    // while parked at a destination keep working: the valve above still owns
    // that case, and a trip never picked (stamp 0) is never suppressed.
    // Bags under pressure always get through: a full bag must reach a vendor.
    if (getQualifier() == std::to_string((uint32)TravelDestinationPurpose::Vendor) &&
        !BagPressureVendorTrip(ai) &&
        VendorTripSuppressedByRecentTrip(
            AI_VALUE2(time_t, "manual time", "vendor trip since"), time(0)))
        return false;

    // Time-boxed blacklist set by MoveToTravelTargetAction on repeated move
    // failure (and by UnstuckAction when retiring a target): ManualSetValue
    // has no expiry, so the timestamp recorded alongside is what ends the park.
    // The timestamp is the authority, not the flag: the flag is parked
    // per-purpose and survives picks, resets and expiries of other purposes
    // (no blanket ClearValues anywhere on the pick path), so a dropped
    // destination stays parked for its window while other purposes keep
    // working. The flag is cleared lazily below once its park has expired.
    // Live night2 pool: a dropped purpose re-picked after a median 1 s,
    // 2,243 same-purpose repicks all under 300 s.
    std::string const parkKey = getQualifier().empty() ? "quest" : getQualifier();
    if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + parkKey) > time(0))
        return false;

    if (AI_VALUE2(bool, "no active travel destinations", parkKey))
        RESET_AI_VALUE2(bool, "no active travel destinations", parkKey);

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (!isAllowed())
    {
        ai->TellDebug(ai->GetMaster(), "Skipped " + GetTravelPurposeName(qualifier) + " because of skip chance", "debug travel");
        return false;
    }

    return true;
}

bool RequestTravelTargetAction::isAllowed() const
{
    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));

    switch (actionPurpose)
    {
    case TravelDestinationPurpose::Repair:
    case TravelDestinationPurpose::Vendor:
    case TravelDestinationPurpose::AH:
        return urand(1, 100) < 90;
    case TravelDestinationPurpose::Mail:
        if (!AI_VALUE(bool, "should get money"))
            return urand(1, 100) < 30;
        else
            return true;
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherHerbalism:
    case TravelDestinationPurpose::GatherFishing:
        if (bot->GetGroup())
            return urand(1, 100) < 50;
        else
            return urand(1, 100) < 90;
    case TravelDestinationPurpose::Boss:
        return urand(1, 100) < 50;
    case TravelDestinationPurpose::Explore:
        return urand(1, 100) < 10;
    case TravelDestinationPurpose::GenericRpg:
        return urand(1, 100) < 50;
    case TravelDestinationPurpose::Grind:
        return true;
    default:
        return true;
    }
}

bool RequestNamedTravelTargetAction::Execute(Event& event)
{
    std::string travelName = getQualifier();

    WorldPosition center = event.GetOwner() ? event.GetOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel " + getQualifier(), "debug travel");

    if (travelName == "pvp")
    {
        std::string WorldPvpLocation;

        //Number between 0 and 100 synced for all bots that shifts 1 every 10 minutes.
        uint32 pvpLocationNumber = ai->GetFixedBotNumber(BotTypeNumber::WORLD_PVP_LOCATION, 100, 0.1f, true);

        if (pvpLocationNumber < 20) //First 200 minutes
            WorldPvpLocation = "Tarren Mill";
        else if (pvpLocationNumber >= 20 && pvpLocationNumber < 40) //Second 200 minutes
            WorldPvpLocation = "The Barrens";
        else if (pvpLocationNumber >= 40 && pvpLocationNumber < 60) //Third 200 minutes
            WorldPvpLocation = "Silithus";
        else if (pvpLocationNumber >= 60 && pvpLocationNumber < 80) //Fourth 200 minutes
            WorldPvpLocation = "Eastern Plaguelands";
        else                                                        //Last 200 minutes
            WorldPvpLocation = "Strangletorn Vale";

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [travelInfo = PlayerTravelInfo(bot), center, WorldPvpLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, WorldPvpLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 }; //Closest map, grid, cell.
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild meeting")
    {
        // Parse guild MOTD for the meeting time.
        // Meeting: <location> <start time> <end time>
        std::string meetingLocation;
        if (bot->GetGuildId())
        {
            Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
            if (guild)
            {
                std::string motd = guild->GetMOTD();
                auto pos = motd.find("Meeting:");
                if (pos != std::string::npos)
                {
                    std::string body = motd.substr(pos + 8);
                    body.erase(body.begin(), std::find_if(body.begin(), body.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                    std::vector<std::string> tokens;
                    { std::istringstream iss(body); std::string t; while (iss >> t) tokens.push_back(t); }
                    if (tokens.size() >= 3)
                    {
                        tokens.pop_back(); // end time
                        tokens.pop_back(); // start time
                        std::ostringstream loc;
                        for (size_t i = 0; i < tokens.size(); ++i) { if (i) loc << " "; loc << tokens[i]; }
                        meetingLocation = loc.str();
                    }
                }
            }
        }

        if (meetingLocation.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No meeting location found in guild MOTD", "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [travelInfo = PlayerTravelInfo(bot), center, meetingLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, meetingLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 };
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild order")
    {
        GuildOrder order = AI_VALUE(GuildOrder, "guild order");

        if (!order.IsTravelOrder())
        {
            ai->TellDebug(ai->GetMaster(), "No valid guild travel order found", "debug travel");
            return false;
        }

        std::string orderTarget = order.target;

        ai->TellDebug(ai->GetMaster(), "Guild order: " + order.GetTypeName() + " " + orderTarget, "debug travel");

        if (order.type == GuildOrderType::QuestReward)
        {
            uint32 questId = order.questId;
            if (!questId)
            {
                ai->TellDebug(ai->GetMaster(), "QuestReward order has no questId", "debug travel");
                return false;
            }

            QuestStatus questStatus = bot->GetQuestStatus(questId);
            bool questComplete = false;
            bool questInProgress = false;

            if (questStatus == QUEST_STATUS_COMPLETE)
                questComplete = true;
            else if (questStatus == QUEST_STATUS_INCOMPLETE)
                questInProgress = true;

            if (!questComplete && questStatus == QUEST_STATUS_INCOMPLETE)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest && bot->CanRewardQuest(quest, false))
                    questComplete = true;
            }

            std::vector<int32> objectiveEntries;
            std::vector<int32> questGiverEntries;
            std::vector<int32> questTakerEntries;

            if (questInProgress && !questComplete)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest)
                {
                    for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
                    {
                        std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };
                        if (!AI_VALUE2(bool, "need quest objective", Qualified::MultiQualify(qualifier, ",")))
                            continue;

                        if (quest->ReqCreatureOrGOId[objective])
                            objectiveEntries.push_back(quest->ReqCreatureOrGOId[objective]);

                        if (quest->ReqItemId[objective])
                        {
                            std::list<int32> dropList = GAI_VALUE2(std::list<int32>, "item drop list", quest->ReqItemId[objective]);
                            for (int32 entry : dropList)
                                objectiveEntries.push_back(entry);

                            std::list<int32> vendorList = GAI_VALUE2(std::list<int32>, "item vendor list", quest->ReqItemId[objective]);
                            for (int32 entry : vendorList)
                                objectiveEntries.push_back(entry);
                        }
                    }
                }
            }

            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred),
                [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, questId,
                questComplete, questInProgress, objectiveEntries]()
                {
                    PartitionedTravelList list;

                    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                    if (!quest)
                        return list;

                    if (questComplete)
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestTaker, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }
                    else if (questInProgress && !objectiveEntries.empty())
                    {
                        uint32 allObjectiveFlags = (uint32)TravelDestinationPurpose::QuestAllObjective;
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            allObjectiveFlags, objectiveEntries, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                            list[partition].insert(list[partition].end(), points.begin(), points.end());

                        if (list.empty())
                        {
                            subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                                (uint32)TravelDestinationPurpose::Grind, objectiveEntries, false, 1000000.0f);
                            for (auto& [partition, points] : subList)
                                list[partition].insert(list[partition].end(), points.begin(), points.end());
                        }
                    }
                    else
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }

                    return list;
                }
            );

            SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
        }
        else if (order.type == GuildOrderType::Farm || order.type == GuildOrderType::Kill)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [travelInfo = PlayerTravelInfo(bot), center, orderTarget, partitions = travelPartitions]()
                {
                    PartitionedTravelList list;

                    uint32 foundItemId = GuildOrderValue::FindItemByName(orderTarget);

                    if (foundItemId)
                    {
                        std::list<int32> dropEntries = GAI_VALUE2(std::list<int32>, "item drop list", foundItemId);

                        if (!dropEntries.empty())
                        {
                            std::vector<int32> gatherEntries, mobEntries;
                            for (int32 entry : dropEntries)
                            {
                                if (entry < 0)
                                    gatherEntries.push_back(entry);
                                else
                                    mobEntries.push_back(entry);
                            }

                            // Check which gathering skills the bot actually has. Skinning is not one
                            // of them here: it has no travel destination (a skin comes off the bot's own
                            // kill), so a skinner-only bot falls through to the mob hunt below.
                            bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                            bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                            bool hasAnyGathering = hasHerbalism || hasMining;

                            // Bot has a gathering skill: prioritize gather nodes.
                            if (!gatherEntries.empty() && hasAnyGathering)
                            {
                                // Only query gather purposes the bot can actually use.
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // If entry-based gather lookup failed, try unfiltered gather by purpose
                            // (the travel manager may index nodes by their own entry, not drop-source entry).
                            if (list.empty() && hasAnyGathering && !gatherEntries.empty())
                            {
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (list.empty() && hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // Fall back to mob drops only if no gather nodes were found or bot has no gathering skill.
                            if (list.empty() && !mobEntries.empty() && !hasAnyGathering)
                            {
                                uint32 mobPurpose = (uint32)TravelDestinationPurpose::Grind;

                                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, mobPurpose, mobEntries, false);
                            }
                        }
                    }

                    // Fall back by name: if bot has gathering skills, try gather-only first.
                    if (list.empty())
                    {
                        bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                        bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                        bool hasSkinning = travelInfo.GetCurrentSkill(SKILL_SKINNING) > 0;
                        bool hasAnyGathering = hasHerbalism || hasMining || hasSkinning;

                        // Try gather nodes by name first if bot can gather.
                        if (hasAnyGathering)
                        {
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, false, false, false, false, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }

                        // If still empty, fall back to mobs, bosses and gather nodes.
                        if (list.empty())
                        {
                            bool includeMobs = !hasAnyGathering;
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, includeMobs, false, includeMobs, includeMobs, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::Explore)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [travelInfo = PlayerTravelInfo(bot), center, orderTarget]()
                {
                    PartitionedTravelList list;
                    for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, true, false, false, false, false, false))
                    {
                        std::list<uint8> chancesToGoFar = { 10,50,90 };
                        WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                        if (!point) continue;
                        list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::AuctionHouse)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
                {
                    PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                    for (auto& [partition, travelPoints] : list)
                    {
                        travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [](TravelPoint point)
                            {
                                EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                                if (!dest->GetCreatureInfo())
                                    return true;

                                if (dest->GetCreatureInfo()->npc_flags & UNIT_NPC_FLAG_AUCTIONEER)
                                    return false;

                                return true;
                            }), travelPoints.end());
                    }
                    return list;
                });
        }
        else
        {
            return false;
        }

        SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
    }
    else if (travelName.find("trainer") == 0)
    {
        TrainerType type = TRAINER_TYPE_CLASS;

        if (travelName == "trainer mount")
            type = TRAINER_TYPE_MOUNTS;
        if (travelName == "trainer trade")
            type = TRAINER_TYPE_TRADESKILLS;
        if (travelName == "trainer pet")
            type = TRAINER_TYPE_PETS;

        std::vector<int32> trainerEntries = AI_VALUE2(std::vector <int32>, "available trainers", type);

        if (trainerEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No trainer entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [entries = trainerEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Trainer, entries, false);
            });
    }
    else if (travelName == "mount")
    {
        std::vector<int32> mountVendorEntries = AI_VALUE(std::vector <int32>, "available mount vendors");

        if (mountVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No vendor entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [entries = mountVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else if (travelName == "reagent vendor")
    {
        std::set<int32> reagentVendorEntrySet;
        std::vector<uint32> missingReagents = NeedsProfessionReagentsValue::GetMissingReagents(ai);
        for (uint32 reagentId : missingReagents)
        {
            std::list<int32> vendorEntries = GAI_VALUE2(std::list<int32>, "item vendor list", reagentId);
            for (int32 entry : vendorEntries)
                reagentVendorEntrySet.insert(entry);
        }

        std::vector<int32> reagentVendorEntries(reagentVendorEntrySet.begin(), reagentVendorEntrySet.end());

        if (reagentVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No reagent vendor entries found", "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [entries = reagentVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else
    {
        uint32 useFlags;

        if (travelName == "city")
            useFlags = NPCFlags::UNIT_NPC_FLAG_BANKER | NPCFlags::UNIT_NPC_FLAG_BATTLEMASTER | NPCFlags::UNIT_NPC_FLAG_AUCTIONEER;
        else if (travelName == "tabard")
            useFlags = NPCFlags::UNIT_NPC_FLAG_TABARDDESIGNER;
        else if (travelName == "petition")
            useFlags = NPCFlags::UNIT_NPC_FLAG_PETITIONER;


        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [cityFlags = useFlags, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                for (auto& [partition, travelPoints] : list)
                {
                    travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [cityFlags](TravelPoint point)
                        {
                            EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                            if (!dest->GetCreatureInfo())
                                return true;

                            if (dest->GetCreatureInfo()->npc_flags & cityFlags)
                                return false;

                            return true;
                        }), travelPoints.end());
                }
                return list;
            });
    }

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition",
        travelName == "guild meeting" ? "should travel named::guild meeting" :
        travelName == "guild order" ? "should travel named::guild order" :
        event.GetSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestNamedTravelTargetAction::isAllowed() const
{
    std::string name = getQualifier();
    if (name == "city")
    {
        if (urand(1, 100) > 10)
            return false;
        return true;
    }
    else if (name == "pvp")
    {
        if (urand(0, 4))
            return false;
        return true;
    }
    else if (name == "guild meeting")
        return true;
    else if (name == "reagent vendor")
        return true;
    else if (name == "guild order")
        return true;
    else if (name == "mount")
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name.find("trainer") == 0)
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name == "tabard")
        return true;
    else if (name == "petition")
        return true;

    return false;
}

bool RequestQuestTravelTargetAction::Execute(Event& event)
{
    WorldPosition center = event.GetOwner() ? event.GetOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel quest", "debug travel");

    // Both search radii below scale with level, which keeps a low level bot near
    // home - a reasonable aim, undone by where the floor sits. At level 1 the
    // pickup radius was 410 yards and the active-quest radius 1075; a starting
    // zone is several thousand across. The bot took a quest, drifted away from
    // the giver while grinding, and could no longer see it: no destination came
    // back, `request quest travel target` returned false, and the engine fell
    // through to `attack anything` for good. Measured on a live realm: ten bots
    // between level 1 and 7 held 23 completed quests between them and logged not
    // one travel event, while the level 10-60 population - radius 8500 upwards -
    // travelled normally. A floor large enough to cover the zone you are standing
    // in fixes that without giving a level 1 bot the run of the continent.
    std::vector<std::tuple<uint32, int32, float>> destinationFetches = { {(uint32)TravelDestinationPurpose::QuestGiver, 0, std::max(2000.f, 400.f + bot->GetLevel() * 10.f)} };

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (player->GetMapId() != bot->GetMapId())
            continue;

        if (!PlayerbotAIStorage::Instance().GetAI(player))
            continue;

        QuestStatusMap& questMap = player->getQuestStatusMap();

        bool onlyClassQuest = bot == player && !urand(0, 10);

        //Find destinations related to the active quests.
        for (auto& [questId, questStatus] : questMap)
        {
            uint32 flag = 0;
            if (questStatus.m_rewarded)
                continue;

            Quest const* questTemplate = sObjectMgr.GetQuestTemplate(questId);

            if (!questTemplate)
                continue;

            if (player->CanRewardQuest(questTemplate, false))
            {
                // Leave-the-valley hand-ins wait before the search runs: a
                // pool bot below 10 only walks to a taker whose quest is
                // rated at most one above its own level
                // (QuestTakerTripFits, same +1 as the grind order cap and
                // the WouldAcceptQuest gate - same quest-half condition, so
                // the two cannot drift). Skipping the fetch keeps the empty
                // taker search - and its 10-minute purpose park - from
                // hiding same-valley hand-ins that are ready now; the
                // destination IsPossible gate below re-checks the same rule
                // with the taker's area once the search runs.
                if (!ai::QuestTakerTripFits((int)questTemplate->GetQuestLevel(), false,
                    bot->GetLevel(), sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster()))
                    continue;

                // A hand-in trip whose moves kept failing is parked per quest by the
                // stuck-hand-in fallback (MoveToTravelTargetAction): the taker is not
                // walkable to, so the search stops offering it for the park window.
                // Other quests, objectives and givers are unaffected. Reading the
                // park only when one exists keeps the value store from growing a
                // "manual time" entry per quest the bot has ever held.
                std::string const parkKey = "no quest hand in until::" + std::to_string(questId);
                if (!HAS_AI_VALUE2("manual time", parkKey) || AI_VALUE2(time_t, "manual time", parkKey) <= time(0))
                    flag = (uint32)TravelDestinationPurpose::QuestTaker;
            }
            else
            {
                for (uint32 objective = 0; objective < 4; objective++)
                {
                    TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << (objective + 1));

                    std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };

                    if (AI_VALUE2(bool, "group or", "following party,need quest objective::" + Qualified::MultiQualify(qualifier, ","))) //Noone needs the quest objective.
                        flag = flag | (uint32)purposeFlag;
                }
            }

            if (!flag)
                continue;

            // Quadratic in level, so a level 20 bot searches thirty times further
            // than a level 1 one. The quest giver a bot has to walk back to is by
            // definition inside its own zone, whatever its level.
            float questRange = 1000.f + (bot->GetLevel() * bot->GetLevel()) * 75.f;
            if (bot->GetLevel() > 5)
                questRange = std::max(5000.f, questRange);
            else
                questRange = std::min(1500.f, questRange);
            destinationFetches.push_back({ flag, questId, questRange });

            if (onlyClassQuest && destinationFetches.size() > 1) //Only do class quests if we have any.
            {
                Quest const* firstQuest = sObjectMgr.GetQuestTemplate(std::get<1>(destinationFetches[1]));

                if (firstQuest->GetRequiredClasses() && !questTemplate->GetRequiredClasses())
                    continue;

                if (!firstQuest->GetRequiredClasses() && questTemplate->GetRequiredClasses())
                    destinationFetches = { destinationFetches.front() };
            }
        }
    }

    // A destination is picked by distance alone: SetBestTarget walks the
    // partitions from near to far and takes the first active point in the
    // nearest one. An objective is close by nature - the bot is standing where
    // it grinds - while the giver it has to return to is back in town. So
    // objectives win nearly every time. Measured over 220 minutes with ~1000
    // bots: 392 objective journeys an hour against 119 turn-ins, while 740
    // quests an hour were accepted. Five taken for every one handed in, so the
    // log can only fill; 751 bots sat at the cap and 138 of them held twenty
    // finished quests they could no longer act on. That also costs them their
    // gear, quest rewards being the only source of it.
    //
    // So once a bot is carrying finished work, the turn-in stops competing on
    // distance and simply wins. Counted here rather than taken from
    // getQuestStatusMap().size(), which includes already rewarded entries and
    // reads above the cap - the probe below saw an average of 22 against a
    // limit of 20.
    bool takerOnly = false;
    {
        uint32 finished = 0, active = 0;
        for (auto& [questId, questStatus] : bot->getQuestStatusMap())
        {
            if (questStatus.m_rewarded)
                continue;

            active++;
            if (questStatus.m_status == QUEST_STATUS_COMPLETE)
                finished++;
        }

        // Quest-log upkeep for masterless random bots: enter hand-in mode at
        // 1 finished quest, then stay in it until the log drains to zero
        // finished quests so one town trip hands everything in instead of
        // ping-ponging out after the first turn-in drops the count to 1. A
        // threshold of 2 left the latch disengaged: on a fresh 500-bot pool 62
        // bots held exactly one finished quest and a single bot held two, so
        // the average backlog sat below the gate and turn-ins kept losing the
        // distance race to objectives. Owned bots keep the old threshold of 5.
        // State lives in the upkeep predicate itself via the facade value store
        // (no new value class).
        bool upkeepBot = sPlayerbotAIConfig.botQuestLogUpkeep &&
            !ai->HasActivePlayerMaster() &&
            sRandomBotFacade.IsRandomBot(bot);
        uint32 handInThreshold = upkeepBot ? 1 : 5;
        if (upkeepBot)
        {
            bool draining = sRandomBotFacade.GetValue(bot, "quest hand-in") != 0;
            if (!draining && finished >= handInThreshold)
            {
                draining = true;
                sRandomBotFacade.SetValue(bot, "quest hand-in", 1, {}, 3600);
            }
            else if (draining && finished == 0)
            {
                draining = false;
                sRandomBotFacade.SetValue(bot, "quest hand-in", 0);
            }
            if (draining || active + 2 >= MAX_QUEST_LOG_SIZE)
            {
                std::vector<std::tuple<uint32, int32, float>> handInOnly;
                for (auto& fetch : destinationFetches)
                    if (std::get<0>(fetch) & (uint32)TravelDestinationPurpose::QuestTaker)
                        handInOnly.push_back(fetch);

                // Only if there is somewhere to hand in. An empty list would fall
                // through to the QuestGiver fetch below and send a bot that cannot
                // accept anything off to collect more. takerOnly silences the same
                // fallback inside the search itself: a taker can survive this
                // fetch and still yield no partition, and the fallback would then
                // hand the bot a quest giver anyway.
                if (!handInOnly.empty())
                {
                    destinationFetches = handInOnly;
                    takerOnly = true;
                }
            }
        }
        else if (finished >= handInThreshold || active + 2 >= MAX_QUEST_LOG_SIZE)
        {
            std::vector<std::tuple<uint32, int32, float>> handInOnly;
            for (auto& fetch : destinationFetches)
                if (std::get<0>(fetch) & (uint32)TravelDestinationPurpose::QuestTaker)
                    handInOnly.push_back(fetch);

            // Only if there is somewhere to hand in. An empty list would fall
            // through to the QuestGiver fetch below and send a bot that cannot
            // accept anything off to collect more. takerOnly silences the same
            // fallback inside the search itself: a taker can survive this fetch
            // and still yield no partition, and the fallback would then hand the
            // bot a quest giver anyway.
            if (!handInOnly.empty())
            {
                destinationFetches = handInOnly;
                takerOnly = true;
            }
        }
    }

    // TEMPORARY probe. 20529 finished quests sit unhanded-in across the bot
    // population and 786 bots have a full quest log, yet the log records only a
    // handful of journeys to a quest taker. The destination is built here, so
    // this records what was on offer; which one then won is already written to
    // bot_events.csv by setNewTarget. Limited to the pinned bots, since a
    // thousand of them would drown the log. Remove once the answer is in.
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
    {
        uint32 takers = 0, objectives = 0, givers = 0, readyToHandIn = 0;
        for (auto& [purpose, questId, range] : destinationFetches)
        {
            if (purpose & (uint32)TravelDestinationPurpose::QuestTaker) takers++;
            else if (purpose & (uint32)TravelDestinationPurpose::QuestGiver) givers++;
            else objectives++;
        }

        for (auto& [questId, questStatus] : bot->getQuestStatusMap())
            if (!questStatus.m_rewarded && questStatus.m_status == QUEST_STATUS_COMPLETE)
                readyToHandIn++;

        sLog.outBasic("QUESTPROBE: %s has %u finished and unhanded-in, %u quests in the log; offered %u taker, %u objective, %u giver destinations",
            bot->GetName(), readyToHandIn, uint32(bot->getQuestStatusMap().size()),
            takers, objectives, givers);
    }

    *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async((sPlayerbotAIConfig.asyncTravelPartitions ? std::launch::async : std::launch::deferred), [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, destinationFetches, takerOnly]()
        {
            PartitionedTravelList list;
            for (auto [purpose, questId, range] : destinationFetches)
            {
                // Quest destinations are keyed by quest id (TravelMgr::AddDestination:
                // id = questId ? questId : entry), and the primary quest-giver fetch
                // carries 0 - so the old `{ questId }` filter asked for a
                // destination keyed 0 and matched nothing at all. Every giver had to
                // come from the empty-filter fallback below, which only runs when
                // nothing else was found. An empty vector is this API's "no entry
                // filter" (see FindDestination, which searches givers the same way).
                std::vector<int32> const entryFilter = questId ? std::vector<int32>{ questId } : std::vector<int32>();
                PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo, purpose, entryFilter, true, range);

                for (auto& [partition, points] : subList)
                    list[partition].insert(list[partition].end(), points.begin(), points.end());
            }

            // A bot that was narrowed to its takers is not looking for new
            // quests: the fetch list holds no quest giver, the log is full or
            // the hand-in latch is engaged, and picking a giver hub would walk
            // it somewhere it cannot accept anything. Let the pick fail and the
            // caller park the purpose instead.
            if (list.empty() && !takerOnly)
            {
                float questGiverRange = (travelInfo.GetLevel() <= 5) ? 1500.0f : ((travelInfo.GetLevel() <= 10) ? 3000.0f : 10000.0f);
                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::QuestGiver, {}, true, questGiverRange);
            }

            return list;
        }
    );

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", "quest");
    SET_AI_VALUE2(std::string, "manual string", "future travel condition", event.GetSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestQuestTravelTargetAction::isAllowed() const
{
    if (AI_VALUE(bool, "should get money"))
        return urand(1, 100) < 90;
    else
        return urand(1, 100) < 95;

    return false;
}

bool FocusTravelTargetAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();

    if (text == "?")
    {
        std::set<uint32> questIds = AI_VALUE(focusQuestTravelList, "focus travel target");
        std::ostringstream out;
        if (questIds.empty())
            out << "No quests selected.";
        else
        {
            out << "I will try to only do the following " << questIds.size() << " quests:";

            for (auto questId : questIds)
            {
                const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

                if (quest)
                    out << ChatHelper::formatQuest(quest);
            }

        }
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    std::set<uint32> questIds = ChatHelper::ExtractAllQuestIds(text);

    if (questIds.empty() && !text.empty())
    {
        if (Qualified::isValidNumberString(text))
            questIds.insert(stoi(text));
        else
        {
            std::vector<std::string> qualifiers = Qualified::getMultiQualifiers(text, ",");

            for (auto& qualifier : qualifiers)
                if (Qualified::isValidNumberString(qualifier))
                    questIds.insert(stoi(text));
        }
    }

    SET_AI_VALUE(focusQuestTravelList, "focus travel target", questIds);

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "travel strategy disabled bot needs this to actually do the quest.");

    if (!ai->HasStrategy("rpg quest", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "rpg quest strategy disabled bot needs this to actually do the quest.");

    std::ostringstream out;
    if (questIds.empty())
        out << "I will now do all quests.";
    else
    {
        out << "I will now only try to do the following " << questIds.size() << " quests:";

        for (auto questId : questIds)
        {
            const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

            if (quest)
                out << ChatHelper::formatQuest(quest);
        }

    }
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    oldTarget->SetExpireIn(1000);

    return true;
}
