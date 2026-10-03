
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "MoveToTravelTargetAction.h"
#include "ChooseTravelTargetAction.h"
#include "TalkToQuestGiverAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/TakerReachabilityCache.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/LootObjectStack.h"
#include "Maps/PathFinder.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TravelRepickPolicy.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include <cstdlib>
#include <iomanip>

using namespace ai;

// Stuck-hand-in fallback (TrySettleUnreachableHandIn). Several bots on the live
// realm walked to a taker they could never reach - the Tower of Azora's Antonas
// Riftgaze, whose navmesh tile has no walkable route to the NPC; every bot stalls
// 18-29 yd away, at the same height, and none ever handed the quest in. Two
// signals settle such a trip, both scoped to a masterless pool bot with a
// finished, rewardable quest:
//
//   * one navmesh probe to the taker itself, the moment the trip comes within
//     range: no complete path to interaction distance means the hand-in is paid
//     now. The answer is shared process-wide (TakerReachabilityCache), so the
//     whole pool settles without each bot running its own query.
//   * the no-progress counters below (a failed move, or the same taker picked
//     again) as the fallback for a taker the probe still calls walkable.
static constexpr float AUTO_HAND_IN_RANGE = 100.0f;           // the bot is at the taker, only the last step fails
static constexpr uint32 AUTO_HAND_IN_FAILS = 3;               // failed moves/re-picks inside one episode
static constexpr time_t AUTO_HAND_IN_AGE = 90;                // seconds since the episode's first failed move
static constexpr time_t AUTO_HAND_IN_PATH_CHECK = 60;         // seconds between navmesh probes for one taker
static constexpr int32 AUTO_HAND_IN_EPISODE = 10 * MINUTE;    // no failed move for this long ends the episode
static constexpr int32 AUTO_HAND_IN_PARK = 30 * MINUTE;       // the quest's hand-in travel park, set when it fires

// The hand-in destination a travel target is currently carrying, when it is a
// taker trip a pool bot could hand in. nullptr for every other purpose.
static QuestTravelDestination* HandInTakerDestination(TravelTarget* target, std::string const& purpose)
{
    if (purpose != "quest")
        return nullptr;

    QuestTravelDestination* destination = dynamic_cast<QuestTravelDestination*>(target->GetDestination());
    if (!destination || destination->GetPurpose() != TravelDestinationPurpose::QuestTaker)
        return nullptr;

    return destination;
}

// The taker a hand-in trip keeps failing to reach, when the fallback may act on
// it: a masterless pool bot carrying a finished, rewardable quest, standing
// within range of the taker but not yet at interaction distance. nullptr for
// anything else, including a bot that has already reached the taker.
static Creature* StuckHandInTaker(PlayerbotAI* ai, uint32 questId, int32 takerEntry)
{
    if (takerEntry <= 0)
        return nullptr;

    Player* bot = ai->GetBot();

    // Pool bots with no real master only. A hired or player-commanded bot keeps the
    // normal hand-in: its master may be walking it to the taker.
    if (!sPlayerbotAIConfig.botQuestLogUpkeep || ai->HasActivePlayerMaster() || !sRandomBotFacade.IsRandomBot(bot))
        return nullptr;

    // Only a finished quest the bot can be paid for right now.
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId) || !bot->CanRewardQuest(quest, false))
        return nullptr;

    Creature* taker = bot->FindNearestCreature((uint32)takerEntry, AUTO_HAND_IN_RANGE);
    if (!taker)
        return nullptr;

    // Reaching interaction distance disproves an earlier mark: the taker is
    // walkable to after all, so the whole pool may walk it again. (Bots only get
    // here on a live hand-in trip, so this is the one place a stale mark - a fixed
    // mesh, a moved spawn - can be retired early.)
    if (bot->GetDistance(taker) <= INTERACTION_DISTANCE)
    {
        TakerReachabilityCache::Instance().Clear(takerEntry);
        return nullptr;
    }

    return taker;
}

// One navmesh query to the taker's own position, the same query the movement
// generator makes. NOPATH is a hole in the mesh, and a partial path
// (INCOMPLETE) or a path that simply ends beyond interaction range both mean the
// bot can never talk to the NPC from ground it is able to stand on. When the
// query did not use the navmesh at all (a tile that is not loaded), it has
// nothing to say and the trip keeps its normal walk.
static bool TakerUnwalkable(Player* bot, Creature* taker)
{
    PathFinder path(bot);
    path.calculate(taker->GetPositionX(), taker->GetPositionY(), taker->GetPositionZ(), false);

    PathType const type = path.getPathType();
    if (type & PATHFIND_NOT_USING_PATH)
        return false;

    if (type & PATHFIND_NOPATH)
        return true;

    Vector3 const end = path.getActualEndPosition();
    return taker->GetDistance(end.x, end.y, end.z) > INTERACTION_DISTANCE;
}

// Pay the quest out without a talk and stop the search from offering this taker
// again for a while. `reason` lands in the QuestAutoHandIn event.
bool MoveToTravelTargetAction::SettleUnreachableTakerHandIn(PlayerbotAI* ai, uint32 questId, int32 takerEntry, std::string const& reason)
{
    Player* bot = ai->GetBot();
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest)
        return false;

    // Park this quest's hand-in travel (per quest, not the whole purpose): the taker
    // is not walkable to, so the search must stop offering it. Read by
    // RequestQuestTravelTargetAction. The nearby-service hand-in does not read the
    // park, so a bot that does end up next to its taker is still paid normally.
    ai->GetAiObjectContext()->GetValue<time_t>("manual time", "no quest hand in until::" + std::to_string(questId))->Set(time(0) + AUTO_HAND_IN_PARK);

    Creature* taker = bot->FindNearestCreature((uint32)takerEntry, AUTO_HAND_IN_RANGE);
    if (!taker)
        return false;

    // Same reward path as a talk-in: the reward choice (best item for the class and
    // spec) and the core Player::RewardQuest call, which carries the XP, money and
    // reputation. Returns false when no reward was actually given.
    if (!TalkToQuestGiverAction::RewardFinishedQuest(ai, quest, taker))
        return false;

    sPlayerbotAIConfig.logEvent(ai, "QuestAutoHandIn", quest->GetTitle(),
        std::to_string(questId) + ":" + std::to_string(takerEntry) + ":" + reason);

    return true;
}

void MoveToTravelTargetAction::CountHandInNoProgress(PlayerbotAI* ai, uint32 questId, int32 takerEntry)
{
    if (!StuckHandInTaker(ai, questId, takerEntry))
        return;

    // Failure episode, keyed per quest: one unreachable taker cannot settle another
    // quest's hand-in, and a repeatable quest gets a fresh episode once a window
    // passes without a failed move. The value counts failed moves and re-picks,
    // the data holds the episode's first failure.
    Player* bot = ai->GetBot();
    std::string const key = "quest hand in fail::" + std::to_string(questId);
    uint32 const fails = sRandomBotFacade.GetValue(bot, key);
    std::string const sinceStr = sRandomBotFacade.GetData(bot->GetGUIDLow(), key);
    time_t const since = sinceStr.empty() ? time(0) : static_cast<time_t>(std::strtoul(sinceStr.c_str(), nullptr, 10));
    sRandomBotFacade.SetValue(bot, key, fails + 1, std::to_string(since), AUTO_HAND_IN_EPISODE);
}

bool MoveToTravelTargetAction::TrySettleUnreachableHandIn(TravelTarget* target, std::string const& purpose)
{
    QuestTravelDestination* destination = HandInTakerDestination(target, purpose);
    if (!destination)
        return false;

    uint32 const questId = destination->GetQuestId();
    int32 const takerEntry = destination->GetEntry();

    Creature* taker = StuckHandInTaker(ai, questId, takerEntry);
    if (!taker)
        return false;

    time_t const now = time(0);
    Player* bot = ai->GetBot();

    // One navmesh probe answers the whole pool: the mark a previous probe left
    // settles this hand-in now, without touching the query.
    if (TakerReachabilityCache::Instance().IsUnreachable(takerEntry, now))
        return SettleUnreachableTakerHandIn(ai, questId, takerEntry, "nopath");

    // Otherwise probe once per bot per taker: a reachable taker must not be
    // queried every tick while the bot walks the last stretch to it.
    AiObjectContext* context = ai->GetAiObjectContext();
    std::string const probeKey = "taker navmesh probe::" + std::to_string(takerEntry);
    bool const probeDue = !context->HasValue("manual time", probeKey) ||
        context->GetValue<time_t>("manual time", probeKey)->Get() <= now - AUTO_HAND_IN_PATH_CHECK;

    if (probeDue)
    {
        context->GetValue<time_t>("manual time", probeKey)->Set(now);

        if (TakerUnwalkable(bot, taker))
        {
            TakerReachabilityCache::Instance().MarkUnreachable(takerEntry, now);
            return SettleUnreachableTakerHandIn(ai, questId, takerEntry, "nopath");
        }
    }

    // Fallback for a taker the probe called walkable (or has not probed yet):
    // three failed moves or re-picks without ever reaching interaction range.
    std::string const key = "quest hand in fail::" + std::to_string(questId);
    uint32 const fails = sRandomBotFacade.GetValue(bot, key);
    if (fails < AUTO_HAND_IN_FAILS)
        return false;

    std::string const sinceStr = sRandomBotFacade.GetData(bot->GetGUIDLow(), key);
    time_t const since = sinceStr.empty() ? now : static_cast<time_t>(std::strtoul(sinceStr.c_str(), nullptr, 10));
    if (now - since < AUTO_HAND_IN_AGE)
        return false;

    return SettleUnreachableTakerHandIn(ai, questId, takerEntry, "unreachable");
}

bool MoveToTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
    {
        ai->TellDebug(ai->GetMaster(), "The target is ready to travel start now.", "debug travel");
        target->SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
    }

    target->CheckStatus();

    if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return true;

    WorldPosition botLocation(bot);
    WorldPosition location = *target->getPosition();

    Group* group = bot->GetGroup();
    if (ai->IsGroupLeader() && !urand(0, 1) && !bot->IsInCombat())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member == bot)
                continue;

            if (!member->IsAlive())
                continue;

            if (!member->IsMoving())
                continue;

            if (PlayerbotAIStorage::Instance().GetAI(member) &&
                !(PlayerbotAIStorage::Instance().GetAI(member)->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || PlayerbotAIStorage::Instance().GetAI(member)->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                continue;

            WorldPosition memberPos(member);
            WorldPosition targetPos = *target->getPosition();

            float memberDistance = std::min(botLocation.distance(memberPos), location.distance(memberPos));

            if (memberDistance < 50.0f)
                continue;
            if (memberDistance > sPlayerbotAIConfig.reactDistance * 20)
                continue;

           // float memberAngle = botLocation.GetAngleBetween(targetPos, memberPos);

           // if (botLocation.GetMapId() == targetPos.GetMapId() && botLocation.GetMapId() == memberPos.GetMapId() && memberAngle < M_PI_F / 2) //We are heading that direction anyway.
           //     continue;

            if (!urand(0, 5))
            {
                std::ostringstream out;
                if ((ai->GetMaster() && !bot->GetGroup()->IsMember(ai->GetMaster()->getObjectGuid())) || !ai->HasActivePlayerMaster())
                    out << "Waiting a bit for ";
                else
                    out << "Please hurry up ";

                out << member->GetName();

                if (PlayerbotAIStorage::Instance().GetAI(bot) && !ai->HasActivePlayerMaster())
                {
                    out << " who is " << round(memberDistance) << "y away";
                    if (!memberPos.GetAreaName().empty())
                        out << " in " << memberPos.GetAreaName();
                }

                // "Waiting a bit for X" is AI narration between pool bots while a
                // group travels together: nobody asked to read it. A real player
                // waiting on their own party still gets the nudge.
                if (isRealPlayer_Helper(GetMaster()))
                    ai->TellPlayerNoFacing(GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            // Introduce a random delay between 80% and 120% of maxWaitForMove to make waiting more natural
            uint32 randomDelay = sPlayerbotAIConfig.maxWaitForMove * (urand(80, 120) / 100.0f);
            target->SetExpireIn(target->GetTimeLeft() + randomDelay);

            SetDuration(randomDelay);

            // Occasionally face the member and perform an emote
            if (urand(0, 3) == 0) { // 25% chance to emote
                bot->SetFacingToObject(member);
                uint32 emoteChoice = urand(0, 2);
                switch (emoteChoice) {
                    case 0:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        break;
                    case 1:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        break;
                    case 2:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                        break;
                }
            }

            return true;
        }
    }

    std::string const purpose = AI_VALUE2(std::string, "manual string", "future travel purpose");

    // A hand-in trip that has come within range of a taker the navmesh cannot
    // reach is settled here, before the bot walks another lap around it.
    if (TrySettleUnreachableHandIn(target, purpose))
        return false;

    float x = location.getX();
    float y = location.getY();
    float z = location.getZ();
    float mapId = location.GetMapId();

    if (botLocation.GetMapId() == location.GetMapId() && botLocation.sqDistance2d(location) < 10000.0f)
    {
        float maxDistance = target->GetDestination()->GetRadiusMin();

        float angle = 2 * M_PI * urand(0, 100) / 100.0;
        float mod = urand(50, 100) / 100.0;

        x += cos(angle) * maxDistance * mod;
        y += sin(angle) * maxDistance * mod;

        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Moving to ";
            out << target->GetDestination()->GetTitle();
            if (!(*target->getPosition() == WorldPosition()))
            {
                out << " at " << uint32(target->getPosition()->distance(bot)) << "y";
            }
            if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << " for " << (target->GetTimeLeft() / 1000) << "s";
            if (target->GetRetryCount(true))
                out << " (move retry: " << target->GetRetryCount(true) << ")";
            else if (target->GetRetryCount(false))
                out << " (retry: " << target->GetRetryCount(false) << ")";
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    bool canMove = MoveTo(mapId, x, y, z, false, false);

    if (!canMove)
    {
        target->IncRetry(true);

        // One line per travel target rather than per attempt: a wedged bot
        // re-enters this action every tick, and IncRetry steps by 2, so the
        // first failure is exactly 2. The drop below carries the final depth.
        // The info2 field stays the remaining distance (parsers read it), with
        // the path bucket from a fresh navmesh probe appended after a colon
        // (nopath = mesh hole, incomplete = partial path, not-using-path =
        // unloaded tile, complete = full path whose dispatch still failed;
        // crossmap = destination on another map, never probed). Finding 14:
        // without it NOPATH vs INCOMPLETE vs mmap-hole cannot be separated
        // from the CSV.
        if (target->GetRetryCount(true) == 2)
        {
            std::string failDetail = std::to_string((int32)botLocation.distance(location));
            failDetail += ":";
            if (location.GetMapId() != bot->GetMapId())
                failDetail += "crossmap";
            else
            {
                PathFinder probe(bot);
                probe.calculate(location.getX(), location.getY(), location.getZ(), false);
                failDetail += TravelMoveFailPathTag((uint32_t)probe.getPathType());
            }
            sPlayerbotAIConfig.logEvent(ai, "TravelMoveFailed", purpose, failDetail);
        }

        // A failed move toward a hand-in taker is one no-progress episode for that
        // quest, the same episode a re-picked taker counts (ChooseTravelTargetAction).
        // Settling on it pays the quest, so nothing is left to walk to and the next
        // tick may pick again.
        if (QuestTravelDestination* destination = HandInTakerDestination(target, purpose))
            CountHandInNoProgress(ai, destination->GetQuestId(), destination->GetEntry());

        if (TrySettleUnreachableHandIn(target, purpose))
            return false;

        if (target->IsMaxRetry(true))
        {
            ai->TellDebug(ai->GetMaster(), "The target is unreachable, dropping it so other travel still works.", "debug travel");
            // Six failed moves in a row means this spot is effectively
            // unreachable from here (no path, other continent, geometry).
            // Drop the target outright and blacklist just this purpose for 5
            // min (the per-kind give-up window, ReachTargetActions.h): other
            // purposes (vendor/repair/quest/grind) stay requestable, and the
            // same destination is not re-picked at once. A COOLDOWN on the
            // active target would instead freeze ALL travel (IsActive stays
            // true, requests gate on it) for the whole window.
            sPlayerbotAIConfig.logEvent(ai, "TravelTargetDropped", purpose, std::to_string(target->GetRetryCount(true)));
            target->SetForced(false);
            sTravelMgr.SetNullTravelTarget(target);
            RESET_AI_VALUE(bool, "travel target active");
            // Time-boxed, not permanent: ManualSetValue has no expiry, so
            // record when the blacklist was set and let the request gate clear
            // it after 5 min. Filed under the park key the gate reads back
            // ("quest" for the quest errand, which carries no qualifier), so
            // an empty purpose parks the quest errand instead of nothing.
            // The park survives picks of other purposes (no blanket
            // ClearValues on the pick path); only this purpose stands down.
            std::string const parkKey = TravelPurposeParkKey(purpose);
            SET_AI_VALUE2(bool, "no active travel destinations", parkKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + parkKey, time(0) + 5 * MINUTE);
        }
    }
    else
        target->DecRetry(true);

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        WorldPosition* pos = target->getPosition();
        GuidPosition* guidP = dynamic_cast<GuidPosition*>(pos);

        std::string name = (guidP && guidP->GetWorldObject(bot->GetInstanceId())) ? chat->formatWorldobject(guidP->GetWorldObject(bot->GetInstanceId())) : "travel target";

        if (mapId == bot->GetMapId())
        {
            ai->Poi(x, y, name);
        }
        else
        {
            LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
            if (!lastMove.lastPath.empty() && lastMove.lastPath.GetBack().distance(location) < 20.0f)
            {
                for (auto& p : lastMove.lastPath.GetPointPath())
                {
                    if (p.GetMapId() == bot->GetMapId())
                        ai->Poi(p.getX(), p.getY(), name);
                }
            }
        }
    }

    return canMove;
}

bool MoveToTravelTargetAction::isUseful()
{
    if (TravelBlockedInsideInstance(ai))
        return false;

    if (!bot->IsAlive()) // a ghost travelling or picking fights instead of going for its corpse
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "travel target traveling") && AI_VALUE(TravelTarget*, "travel target")->GetStatus() != TravelStatus::TRAVEL_STATUS_READY)
        return false;

    if (bot->IsTaxiFlying())
        return false;

    if (MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 10)
        if (bot->IsMoving())
            return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
    {
        auto conditions = travelTarget->GetConditions();
        for (auto& cond : conditions)
        {
            if (cond == "should travel named::guild order")
                return false;
        }
    }

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->getObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            if (!travelTarget->IsForced())
                return false;

    WorldPosition travelPos(*travelTarget->getPosition());

    if (travelPos.isDungeon() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid()) && sTravelMgr.MapTransDistance(bot, travelPos, true) < sPlayerbotAIConfig.sightDistance && !AI_VALUE2(bool, "group and", "near leader"))
        return false;

    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    if (!travelTarget->IsForced())
        if (!CanFreeMoveValue::CanFreeMoveTo(ai, *travelTarget->getPosition()))
            return false;

    return true;
}
