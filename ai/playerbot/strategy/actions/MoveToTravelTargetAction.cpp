
#include "playerbot/GroupMembers.h"
#include "playerbot/TaxiRouteGuard.h"
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
#include "playerbot/TravelLegChainPolicy.h"
#include "playerbot/policy/travel/FlightErrandPolicy.h"
#include "playerbot/policy/travel/OrderFocusPolicy.h"
#include "playerbot/policy/travel/MesaLiftPolicy.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "GuildOrderBuyAction.h"
#include "runtime/GuildOrderGiveUpPolicy.h"
#include "runtime/GuildOrderSourcesPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/HireLifecycle.h"
#include "runtime/AltBotService.h"
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

// No-displacement watchdog: a move that reports success while the bot stands
// still (spline into geometry, collapsed shortcut) must fill the same failure
// budget a refused move does. Same anchor + window as the "move to loot"
// watchdog (MovementActions.cpp): a walking bot covers this in about a
// second, so only a bot that truly stands still ever trips it.
static constexpr float TRAVEL_NO_PROGRESS_RADIUS = 2.0f;  // yards
static constexpr time_t TRAVEL_NO_PROGRESS_TIMEOUT = 5;   // seconds

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
    WorldPosition::ExcludeSteepSlopes(path);
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

// A service NPC inside its arrival radius counts as arrived, even when the
// walk point sits inside geometry (the Orgrimmar auctioneer stalls: the bot
// 5-8 yd out, path "incomplete"). Creature entries resolve by spawn lookup
// in the wide arrival radius; game-object mailboxes by entry; a
// destination with no NPC entry falls back to the destination point
// itself. Arrival flips the target to WORK so the service action runs.
bool MoveToTravelTargetAction::CheckServiceArrival(TravelTarget* target, std::string const& purpose)
{
    if (!ServiceTripIsServicePurpose(purpose))
        return false;

    int32 const entry = target->GetEntry();
    WorldPosition const location = *target->getPosition();

    if (entry == 0)
    {
        if (location.GetMapId() != bot->GetMapId())
            return false;
        if (WorldPosition(bot).distance(location) > SERVICE_TRIP_COUNTER_ARRIVAL_YD)
            return false;
        target->SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
        return true;
    }

    if (entry > 0)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate((uint32)entry);
        uint32 const flags = info ? info->npc_flags : 0;
        float const arrival = ServiceTripArrivalRadius(flags);
        if (bot->FindNearestCreature((uint32)entry, arrival))
        {
            target->SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
            return true;
        }
        return false;
    }

    if (bot->FindNearestGameObject((uint32)-entry, SERVICE_TRIP_COUNTER_ARRIVAL_YD))
    {
        target->SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
        return true;
    }
    return false;
}

// Land on the navmesh, not on the npc's own spot: npcs on upper floors and
// balconies the mmaps never meshed (Stormwind trainers) left the bot above
// the mesh, where every path probe fails and the bot stood until rescued.
// Narrowest height window first, and the landing must walk back to the npc:
// with one 60 yd window the nearest poly under a Stormwind trainer was the
// mesh beneath the city, and 94 of 174 rescues (live 2026-10-09) landed
// there and failed every move again.
static bool FindRescueLanding(Player* bot, WorldPosition const& location, WorldPosition& landing)
{
    if (!location.isMmapLoaded(bot->GetInstanceId()))
        return false;
    for (float const height : { 5.0f, 15.0f, 30.0f, 60.0f })
    {
        WorldPosition candidate = location;
        if (!candidate.ClosestCorrectPoint(30.0f, height, bot->GetInstanceId()))
            continue;
        if (location.isPathTo(candidate.GetPathTo(location, bot), 10.0f, 10.0f))
        {
            landing = candidate;
            return true;
        }
    }
    return false;
}

// Rescue for a service trip at the drop threshold (IsMaxRetry): teleport to
// the exact destination point instead of dropping + parking. Same
// unwatched-either-end rule as the passive walk hop in MoveTo2, same map
// only (a walk hop stands in for a walk; cross-map never does), random
// masterless pool bot only, once per 30 min. The order-vendor leg is picked
// forced, so its pin is exempt here: the drop below unforces anyway and
// every re-pick re-forces. The rescue clears the move budget like a fresh
// pick so the next tick walks the last yards instead of dropping at once;
// the event makes it measurable.
bool MoveToTravelTargetAction::TryRescueServiceTrip(TravelTarget* target, std::string const& purpose)
{
    if (!ServiceTripIsServicePurpose(purpose))
        return false;
    if (!target->IsMaxRetry(true))
        return false;
    if (target->IsForced() && !ai::ServiceTripRescueIgnoresForced(purpose))
        return false;

    WorldPosition location = *target->getPosition();
    if (location.GetMapId() != bot->GetMapId())
        return false;
    if (!bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsBeingTeleported())
        return false;
    if (ai->HasRealPlayerMaster() || !sRandomBotFacade.IsFreeBot(bot))
        return false;
    WorldPosition const botPos(bot);
    if (ai->HasPlayerNearby(botPos, sPlayerbotAIConfig.reactDistance) || ai->HasPlayerNearby(location, sPlayerbotAIConfig.reactDistance))
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();
    time_t const now = time(0);
    if (AI_VALUE2(time_t, "manual time", "service trip rescue") + SERVICE_TRIP_RESCUE_COOLDOWN_SEC > now)
        return false;

    WorldPosition landing;
    if (!FindRescueLanding(bot, location, landing))
        return false;
    location = landing;

    if (!bot->TeleportTo(location.GetMapId(), location.getX(), location.getY(), location.getZ(),
        WorldPosition(bot).GetAngleTo(location)))
        return false;

    SET_AI_VALUE2(time_t, "manual time", "service trip rescue", now);
    target->SetRetry(true, 0);
    sPlayerbotAIConfig.logEvent(ai, "ServiceTripTeleport", purpose,
        std::to_string((int32)WorldPosition(bot).distance(location)));
    return true;
}

// Stuck-travel teleport (AiPlayerbot.StuckTravelTeleport, default on),
// after mod-playerbots NewRpgBaseAction::MoveFarTo: a trip that has made no
// progress until the drop threshold (IsMaxRetry: six failed or no-progress
// moves) teleports to its destination instead of dropping. It closes the
// open-sea standstill: a pool bot swimming at sea level has no walkable mesh
// in reach, so the step, the mesh hop and the trap rescues all refuse, and
// every re-pick fails the same way until the long-stuck rescue (live
// 2026-10-11: 15 runs of 3+ min at z -2, about 102 bot-min). Masterless pool
// bots only (never a real master, an owned or a hired bot), same map, nobody
// watching either end, once per STUCK_TRAVEL_RESCUE_COOLDOWN_SEC per bot.
// Lands on the mesh next to the destination like the service rescue, or on
// the nearest route node within STUCK_TRAVEL_NODE_RANGE_YD of it.
static constexpr time_t STUCK_TRAVEL_RESCUE_COOLDOWN_SEC = 10 * MINUTE;
static constexpr float STUCK_TRAVEL_NODE_RANGE_YD = 200.0f;

bool MoveToTravelTargetAction::TryRescueStuckTravel(TravelTarget* target, std::string const& purpose)
{
    if (!sPlayerbotAIConfig.stuckTravelTeleport)
        return false;
    if (!target->IsMaxRetry(true) || target->IsForced())
        return false;

    WorldPosition const location = *target->getPosition();
    if (location.GetMapId() != bot->GetMapId())
        return false;
    if (!bot->IsAlive() || bot->IsInCombat() || bot->InBattleGround() || bot->IsTaxiFlying() ||
        bot->IsBeingTeleported() || bot->GetTransport())
        return false;
    // Issue #872: an alt-bot gets the pool's same-map stuck recovery (C22).
    if (ai->HasRealPlayerMaster() ||
        (!TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()) &&
         (ai->IsOwnedBot() || !sRandomBotFacade.IsRandomBot(bot))) ||
        TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
        return false;
    WorldPosition const botPos(bot);
    bool const watched = ai->HasPlayerNearby(botPos, sPlayerbotAIConfig.reactDistance) ||
        ai->HasPlayerNearby(location, sPlayerbotAIConfig.reactDistance);
    if (ai::WatchedRescueMayStep(watched, target->IsForced()))
    {
        // A watched rescue refuses the teleport but the walk is unwatched:
        // stepping toward the target instead of returning silently keeps
        // the bot progressing (and logging) instead of freezing in place.
        if (TryStepTowardTarget(location))
        {
            target->SetRetry(true, 1);
            return true;
        }
        return false;
    }
    if (watched)
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();
    time_t const now = time(0);
    if (AI_VALUE2(time_t, "manual time", "stuck travel rescue") + STUCK_TRAVEL_RESCUE_COOLDOWN_SEC > now)
        return false;

    WorldPosition landing;
    std::string landedOn = "mesh";
    if (!FindRescueLanding(bot, location, landing))
    {
        bool found = false;
        for (TravelNode* node : sTravelNodeMap.getNodes(location, STUCK_TRAVEL_NODE_RANGE_YD))
        {
            if (node->IsTransport())
                continue;
            landing = *node->getPosition();
            found = true;
            break;
        }
        if (!found || ai->HasPlayerNearby(landing, sPlayerbotAIConfig.reactDistance))
            return false;
        landedOn = "node";
    }

    if (!bot->TeleportTo(landing.GetMapId(), landing.getX(), landing.getY(), landing.getZ(), botPos.GetAngleTo(landing)))
        return false;

    SET_AI_VALUE2(time_t, "manual time", "stuck travel rescue", now);
    target->SetRetry(true, 0);
    noProgressArmed = false;
    sPlayerbotAIConfig.logEvent(ai, "StuckTravelTeleport", purpose,
        std::to_string((int32)botPos.distance(landing)) + ":" + landedOn + ":" +
        std::to_string((int32)botPos.getZ()) + ">" + std::to_string((int32)location.getZ()));
    return true;
}

// Called once per target on its first failed move (retry 2) with the navmesh
// verdict. Bots with a real master, in a battleground or in combat are left
// alone, like the other rescues. A rescue that does not move the bot still
// resets the streak, so the next try needs three fresh failures. The
// streak lives in the facade value store, like "stuck keep count".
// Off-graph trap (no route node in walking reach, e.g. Lapidis Isle): hearth
// and repop keep the bot on the island when its graveyard is there (live
// 2026-10-09: every repop landed at the island graveyard and the bot walked
// back to the shore). A masterless pool bot nobody watches goes to the nearest
// route node instead, from where every route works.
bool MoveToTravelTargetAction::TeleportToNearestRouteNode()
{
    if (ai->HasRealPlayerMaster() || !sRandomBotFacade.IsFreeBot(bot) || bot->IsBeingTeleported() || bot->IsTaxiFlying())
        return false;
    WorldPosition const botPos(bot);
    if (ai->HasPlayerNearby(botPos, sPlayerbotAIConfig.reactDistance))
        return false;

    for (TravelNode* node : sTravelNodeMap.getNodes(botPos))
    {
        if (node->IsTransport())
            continue;
        WorldPosition const nodePos = *node->getPosition();
        // A node within 300 yd means a mesh pocket on a mapped continent
        // (Stormwind floors, live 2026-10-09: 88-127 yd), not an island: the
        // regular hearth/repop rescue handles those without a teleport.
        if (nodePos.distance(botPos) < 300.0f)
            return false;
        if (ai->HasPlayerNearby(nodePos, sPlayerbotAIConfig.reactDistance))
            return false;
        if (!bot->TeleportTo(nodePos.GetMapId(), nodePos.getX(), nodePos.getY(), nodePos.getZ(), botPos.GetAngleTo(nodePos)))
            return false;
        sPlayerbotAIConfig.logEvent(ai, "OffGraphTeleport", node->getName(), std::to_string((int32)nodePos.distance(botPos)));
        return true;
    }
    return false;
}

bool MoveToTravelTargetAction::TryRescueNoPathTrap(bool noPath, bool offGraph, std::string const& purpose)
{
    if (ai->HasRealPlayerMaster() || bot->InBattleGround() || bot->IsInCombat() || !bot->IsAlive())
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();
    time_t const now = time(0);
    WorldPosition const botPos(bot);
    int32 streak = AI_VALUE2(int32, "manual int", "nopath trap streak");
    WorldPosition const anchor = AI_VALUE2(WorldPosition, "custom position", "nopath trap anchor");
    time_t const since = AI_VALUE2(time_t, "manual time", "nopath trap since");

    bool const nearAnchor = anchor.isValid() && botPos.GetMapId() == anchor.GetMapId() &&
        botPos.sqDistance2d(anchor) <= NOPATH_TRAP_RADIUS_YD * NOPATH_TRAP_RADIUS_YD;
    bool const sameFloor = !anchor.isValid() ||
        fabs(botPos.getZ() - anchor.getZ()) <= NOPATH_TRAP_FLOOR_Z_YD;
    int32 const next = NoPathTrapStreak(streak, noPath, nearAnchor, now - since > NOPATH_TRAP_WINDOW_SECONDS, sameFloor);
    if (noPath && next == 1)
    {
        SET_AI_VALUE2(WorldPosition, "custom position", "nopath trap anchor", botPos);
        SET_AI_VALUE2(time_t, "manual time", "nopath trap since", now);
    }
    SET_AI_VALUE2(int32, "manual int", "nopath trap streak", next);
    if (!IsNoPathTrapped(next))
        return false;

    SET_AI_VALUE2(int32, "manual int", "nopath trap streak", 0);
    sPlayerbotAIConfig.logEvent(ai, "NoPathTrapRescue", purpose, std::to_string(next));
    if (offGraph && TeleportToNearestRouteNode())
    {
        if (TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target"))
            sTravelMgr.SetNullTravelTarget(travelTarget);
        return true;
    }
    Event rescueEvent("travel nopath trapped");
    return ai->DoSpecificAction("unstuck", rescueEvent, true);
}

bool MoveToTravelTargetAction::TravelLegChainDue(PlayerbotAI* ai, Player* bot)
{
    // Pure walk legs only: a chained leg re-enters the same route code
    // (makeShortCut / ClipPath / dispatch), which would clear a transport,
    // swim, flight or mount move. Combat moves re-target every turn and a
    // dungeon or grouped wait stands still by design.
    if (!bot || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsFlying() ||
        bot->IsMounted() || bot->IsBeingTeleported() || bot->GetTransport() ||
        bot->InBattleGround() || bot->IsInWater() || bot->IsUnderwater() ||
        bot->HasMovementFlag(MOVEFLAG_SWIMMING))
        return false;
    if (bot->GetMap() && bot->GetMap()->IsDungeon())
        return false;
    if (Group* group = bot->GetGroup())
        if (!group->IsLeader(bot->getObjectGuid()))
            return false;
    if (ai->GetState() == BotState::BOT_STATE_COMBAT)
        return false;

    // Chain only after a leg was successfully dispatched: a failed move (or
    // a path with fewer than 2 points) never chains, so the caller's retry
    // counting and target drop behave exactly as before.
    AiObjectContext* context = ai->GetAiObjectContext();
    LastMovement const& lastMove = AI_VALUE(LastMovement&, "last movement");
    if (lastMove.moveFailReason != MOVE_FAIL_NONE)
        return false;
    if (lastMove.lastPath.GetPath().size() < 2)
        return false;
    for (PathNodePoint const& point : lastMove.lastPath.GetPath())
        if (!point.IsWalkable())
            return false;

    Movement::MoveSpline const& spline = *bot->movespline;
    char const* origin = spline.GetMovementOrigin();
    bool ownLeg = origin != nullptr && std::strcmp(origin, "DispatchMovement") == 0;
    return TravelLegChainDecide(ownLeg, spline.timeElapsed(), spline.Initialized(), spline.Finalized())
        == TravelLegChainVerdict::Chain;
}

bool MoveToTravelTargetAction::TravelMoveMadeNoProgress()
{
    time_t const now = time(0);
    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

    // A scheduled teleport-hop wait stands still by design: the bot jumped
    // forward and the next move goes out once the hop's travel time passes.
    if (lastMove.nextTeleport > now)
        return false;

    // A transport leg (boat/dock wait) stands still by design.
    for (PathNodePoint const& point : lastMove.lastPath.GetPath())
        if (point.type == PathNodeType::NODE_TRANSPORT)
            return false;

    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float const dx = x - noProgressX;
    float const dy = y - noProgressY;
    bool const anchored = noProgressArmed &&
        dx * dx + dy * dy <= TRAVEL_NO_PROGRESS_RADIUS * TRAVEL_NO_PROGRESS_RADIUS;
    if (!anchored)
    {
        noProgressArmed = true;
        noProgressX = x;
        noProgressY = y;
        noProgressSince = now;
        return false;
    }

    if (now - noProgressSince < TRAVEL_NO_PROGRESS_TIMEOUT)
        return false;

    // Stood inside the radius for the whole window while moves kept
    // reporting success: count it and start a new window, so a continued
    // stand keeps filling the failure budget below.
    noProgressSince = now;
    return true;
}

// Flight plan: decided ONCE per travel target (item 5) in
// DecideFlightPlanForTarget below, stored on manual values, and only read
// (never recomputed) on travel ticks. Walk-to-master staging (item 4): when
// a plan exists the bot walks to the flight master as its intermediate move
// target until inside interaction range, then boards. All zone/level reads
// use the startup taxi-node zone cache (item 6) - no GetArea/vmap on ticks.
static bool ReadFlightPlan(PlayerbotAI* ai, uint32& fromNode, uint32& toNode)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    int32 from = AI_VALUE2(int32, "manual int", "flight from node");
    int32 to = AI_VALUE2(int32, "manual int", "flight to node");
    if (from <= 0 || to <= 0 || from == to)
        return false;
    fromNode = (uint32)from;
    toNode = (uint32)to;
    return true;
}

static void ClearFlightPlan(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    SET_AI_VALUE2(int32, "manual int", "flight from node", (int32)0);
    SET_AI_VALUE2(int32, "manual int", "flight to node", (int32)0);
}

// Decide-once evaluation for the CURRENT travel target. Called exactly when
// a new target is set (ChooseTravelTargetAction::setNewTarget), never on
// travel ticks. Uses the startup taxi-node zone cache and validated area
// levels only - no WorldPosition::GetArea, no vmap load, no money-value
// reads beyond the two cached uint32s below.
void ai::DecideFlightPlanForTarget(PlayerbotAI* ai, Player* bot, TravelTarget const* target)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    ClearFlightPlan(ai);
    if (!ai || !bot || !target || !target->getPosition())
        return;
    if (ai->HasRealPlayerMaster() || !sRandomBotFacade.IsFreeBot(bot))
        return;
    if (!bot->IsAlive())
        return;
    WorldPosition const botLocation(bot);
    WorldPosition const location = *target->getPosition();
    float const tripDistance = botLocation.distance(location);
    if (tripDistance < ai::FLIGHT_TRANSPORT_MIN_TRIP_YD)
        return;

    uint32 const fromNode = sObjectMgr.GetNearestTaxiNode(botLocation.getX(), botLocation.getY(),
        botLocation.getZ(), botLocation.GetMapId(), bot->GetTeam());
    if (!fromNode || !bot->GetTaxi().IsTaximaskNodeKnown(fromNode))
        return;

    TaxiNodesEntry const* fromEntry = sObjectMgr.GetTaxiNodeEntry(fromNode);
    if (!fromEntry || !fromEntry->MountCreatureID[bot->GetTeam() == ALLIANCE ? 1 : 0])
        return;
    if (fromEntry->map_id != botLocation.GetMapId())
        return;

    uint32 const toNode = sObjectMgr.GetNearestTaxiNode(location.getX(), location.getY(),
        location.getZ(), location.GetMapId(), bot->GetTeam());
    if (!toNode || toNode == fromNode)
        return;
    if (!bot->GetTaxi().IsTaximaskNodeKnown(toNode))
        return;

    TaxiNodesEntry const* toEntry = sObjectMgr.GetTaxiNodeEntry(toNode);
    if (!toEntry || !toEntry->MountCreatureID[bot->GetTeam() == ALLIANCE ? 1 : 0])
        return;

    uint32 taxiPath = 0;
    uint32 fare = 0;
    sObjectMgr.GetTaxiPath(fromNode, toNode, taxiPath, fare);
    if (!taxiPath)
        return;

    // Zone ids AND area levels both come from startup caches: the node-zone
    // cache (DBC node -> zone, built from loaded terrain at startup) and the
    // validated zone-level table. No WorldPosition::GetArea, no vmap load.
    uint32 const destZoneId = sTravelMgr.GetTaxiNodeZoneId(toNode);
    if (!destZoneId)
        return;
    int32 destAreaLevel = 0;
    if (!sTravelMgr.TryGetValidatedAreaLevel(destZoneId, destAreaLevel) || destAreaLevel <= 0)
        return;
    AreaTableEntry const* destZone = GetAreaEntryByAreaID(destZoneId);
    bool const destZoneIsCapital = destZone && (destZone->Flags & AREA_FLAG_CAPITAL);
    uint32 const botLevel = bot->GetLevel();
    // Bot's own tile is always resident (the bot stands on it): no vmap load.
    bool const botInCapital = botLocation.HasAreaFlag(AREA_FLAG_CAPITAL);
    if (!ai::FlightTransportDestinationUsable(botLevel, botInCapital, destAreaLevel, destZoneIsCapital))
        return;

    WorldPosition const landPos(toEntry->map_id, toEntry->x, toEntry->y, toEntry->z);
    if (!ai::FlightTransportLegWorthwhile(tripDistance, botLocation.distance(location), landPos.distance(location)))
        return;

    uint32 const trainerReserve = AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::spells);
    if (!ai::FlightTransportAffordable(bot->GetMoney(), fare, trainerReserve))
        return;

    SET_AI_VALUE2(int32, "manual int", "flight from node", (int32)fromNode);
    SET_AI_VALUE2(int32, "manual int", "flight to node", (int32)toNode);
}

static bool TryBoardFlightToTarget(PlayerbotAI* ai, Player* bot, TravelTarget* target,
    WorldPosition const& botLocation, WorldPosition const& location)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    if (ai->HasRealPlayerMaster() || !sRandomBotFacade.IsFreeBot(bot))
        return false;
    if (!bot->IsAlive() || bot->IsInCombat())
        return false;
    if (bot->IsTaxiFlying() || bot->IsBeingTeleported())
        return false;
    if (!target || !target->getPosition())
        return false;

    // Decide-once: read the stored plan, never recompute.
    uint32 fromNode = 0;
    uint32 toNode = 0;
    if (!ReadFlightPlan(ai, fromNode, toNode))
        return false;

    TaxiNodesEntry const* fromEntry = sObjectMgr.GetTaxiNodeEntry(fromNode);
    TaxiNodesEntry const* toEntry = sObjectMgr.GetTaxiNodeEntry(toNode);
    if (!fromEntry || !toEntry)
    {
        ClearFlightPlan(ai);
        return false;
    }
    if (!bot->GetTaxi().IsTaximaskNodeKnown(fromNode) || !bot->GetTaxi().IsTaximaskNodeKnown(toNode))
    {
        ClearFlightPlan(ai);
        return false;
    }
    if (!ai::BotTaxiRouteAllowed({ fromNode, toNode }))
    {
        ClearFlightPlan(ai);
        return false;
    }

    WorldPosition const masterPos(fromEntry->map_id, fromEntry->x, fromEntry->y, fromEntry->z);
    if (masterPos.GetMapId() != botLocation.GetMapId())
    {
        ClearFlightPlan(ai);
        return false;
    }

    // Board only inside interaction range with a usable flight master.
    if (botLocation.distance(masterPos) > INTERACTION_DISTANCE)
        return false;

    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    Creature* flightMaster = nullptr;
    for (ObjectGuid guid : npcs)
    {
        flightMaster = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
        if (flightMaster)
            break;
    }
    if (!flightMaster)
        return false;

    ai->RemoveShapeshift();
    ai->Unmount();
    bot->GetSession()->SendLearnNewTaxiNode(flightMaster);
    if (!bot->ActivateTaxiPathTo({ fromNode, toNode }, flightMaster, 0))
    {
        ClearFlightPlan(ai);
        return false;
    }
    ai::BotInstantFlightFinish(bot, { fromNode, toNode });

    sPlayerbotAIConfig.logEvent(ai, "TaxiFlight", fromEntry->name[LOCALE_enUS], toEntry->name[LOCALE_enUS]);
    ClearFlightPlan(ai);
    return true;
}

// A pool bot whose travel move found no route used to stand and drop the
// target (44% of move failures were nopath, live 2026-10-09). Two cheap steps
// instead, like mod-playerbots NewRpgBaseAction::MoveFarTo:
//  - standing off the walkable mesh (water, under a city, a balcony): hop to
//    the nearest walkable spot within 15 yd;
//  - otherwise walk to a reachable point in the cone toward the target that
//    ends at least 5 yd closer, and try again from there. Every step must
//    close the gap, so the walk cannot loop.
bool MoveToTravelTargetAction::TryStepTowardTarget(WorldPosition const& location)
{
    if (ai->HasRealPlayerMaster() || !sRandomBotFacade.IsFreeBot(bot) || bot->InBattleGround() ||
        bot->IsInCombat() || !bot->IsAlive() || bot->IsTaxiFlying() || bot->GetTransport() ||
        location.GetMapId() != bot->GetMapId())
        return false;

    WorldPosition const botPos(bot);
    if (!botPos.isMmapLoaded(bot->GetInstanceId()))
        return false;

    WorldPosition onMesh = botPos;
    if (!onMesh.ClosestCorrectPoint(2.0f, 3.0f, bot->GetInstanceId()))
    {
        WorldPosition hop = botPos;
        if (hop.ClosestCorrectPoint(15.0f, 10.0f, bot->GetInstanceId()) && !ai->HasPlayerNearby(botPos, sPlayerbotAIConfig.reactDistance) &&
            bot->TeleportTo(hop.GetMapId(), hop.getX(), hop.getY(), hop.getZ(), bot->GetOrientation()))
        {
            sPlayerbotAIConfig.logEvent(ai, "TravelMeshHop", std::to_string((int32)botPos.distance(hop)), "");
            return true;
        }
        return false;
    }

    float const gap = botPos.distance(location);
    float const baseAngle = botPos.getAngleTo(location);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        float const angle = baseAngle + (rand_norm_f() - 0.5f) * M_PI_F;
        float const reach = std::min(gap, 40.0f + rand_norm_f() * 40.0f);
        WorldPosition stone(bot->GetMapId(), botPos.getX() + cos(angle) * reach, botPos.getY() + sin(angle) * reach, botPos.getZ());
        if (!stone.ClosestCorrectPoint(10.0f, 30.0f, bot->GetInstanceId()))
            continue;
        std::vector<WorldPosition> path = botPos.GetPathTo(stone, bot);
        if (path.size() < 2)
            continue;
        WorldPosition const end = path.back();
        if (end.distance(location) + 5.0f >= gap)
            continue;

        bot->GetMotionMaster()->MovePoint(end.GetMapId(), end.getX(), end.getY(), end.getZ(), MOVE_RUN_MODE | MOVE_PATHFINDING);
        WaitForReach(botPos.distance(end));
        // WaitForReach caps at a few seconds; the walk itself may take longer.
        SET_AI_VALUE2(time_t, "manual time", "travel step until", time(0) + 2 + (time_t)(botPos.distance(end) / 7.0f));
        sPlayerbotAIConfig.logEvent(ai, "TravelStepToward", std::to_string((int32)gap), std::to_string((int32)end.distance(location)));
        return true;
    }
    return false;
}

// Lift rides are for masterless pool bots and autonomous alt-bots
// (issue #872, the pool's same-map stuck recovery C22), never hired ones, and
// only when the bot is free to move.
static bool MayRideLift(PlayerbotAI* ai, Player* bot)
{
    if (!bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsBeingTeleported() || bot->GetTransport())
        return false;
    return !ai->HasRealPlayerMaster() &&
        (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()) ||
         (!ai->IsOwnedBot() && sRandomBotFacade.IsRandomBot(bot))) &&
        !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
}

static std::string LiftLogPos(WorldPosition const& pos)
{
    return std::to_string((int32)pos.getX()) + " " + std::to_string((int32)pos.getY()) + " " + std::to_string((int32)pos.getZ());
}

// Mesa lifts (MesaLiftPolicy.h): a trip between a mesa top and the ground
// walks to the shaft of the next car at the bot's level, waits for it, rides
// it and steps off at the other level. The core moves no passenger on a lift,
// so the module does: the bot is marked as standing on the car (watching
// clients carry it along, as they carry a player) and its server position
// follows the car's animation.
// `staged` puts the walk on the shaft.
bool MoveToTravelTargetAction::TryMesaLift(WorldPosition const& location, float& x, float& y, float& z, bool& staged)
{
    // Zones only matter for the zone lifts; look the goal's up only then.
    uint32 const botZone = bot->GetZoneId();
    uint32 goalZone = 0;
    if (botZone == ai::kBarrensZone || botZone == ai::kThousandNeedlesZone)
        if (AreaTableEntry const* area = location.GetArea())
            goalZone = area->ZoneId ? area->ZoneId : area->Id;
    ai::MesaLiftPick const pick = ai::MesaLiftFor(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), botZone,
        location.GetMapId(), location.getX(), location.getY(), location.getZ(), goalZone);
    if (pick.dir == ai::MesaLiftDir::None || !MayRideLift(ai, bot))
        return false;

    bool const fromTop = pick.dir == ai::MesaLiftDir::Down;
    uint32 const now = WorldTimer::getMSTime();
    // The shaft nearest the bot, kept once picked: re-picking walked the bot
    // back and forth between the two shafts.
    if (waitLift != pick.lift || waitFromTop != fromTop)
    {
        waitLift = pick.lift;
        waitFromTop = fromTop;
        waitCar = ai::NearestLiftCar(*pick.lift, bot->GetPositionX(), bot->GetPositionY());
    }
    int const carIndex = waitCar;
    ai::LiftCar const& car = pick.lift->cars[carIndex];
    WorldPosition stop(bot->GetMapId(), car.x, car.y, ai::LiftLevelZ(car, fromTop));
    if (stop.isMmapLoaded(bot->GetInstanceId()))
        stop.ClosestCorrectPoint(8.0f, 6.0f, bot->GetInstanceId());

    staged = true;
    WorldPosition const botPos(bot);
    if (botPos.sqDistance2d(stop) > 4.0f * 4.0f || fabs(botPos.getZ() - stop.getZ()) > 6.0f)
    {
        x = stop.getX();
        y = stop.getY();
        z = stop.getZ();
        return false;
    }

    // Stand at the shaft from here on, never walk on toward the goal: past
    // the shaft the mesh climbs the lift path in the air.
    noProgressArmed = false;
    // The server keeps a car at its spawn, up to 130 yd above or below.
    GameObject* carObject = bot->FindNearestGameObject(car.entry, 150.0f);
    if (!carObject)
    {
        SetDuration(2000);
        return true;
    }

    // Walk onto the parked car with a second to spare; otherwise wait at
    // the shaft for the next one.
    uint32 const parkedLeft = ai::LiftParkedLeft(car, now, fromTop);
    float const step = std::hypot(bot->GetPositionX() - car.x, bot->GetPositionY() - car.y);
    uint32 const walkMs = step > 1.5f ? uint32(step / std::max(bot->GetSpeed(MOVE_RUN), 1.0f) * 1000.0f) + 300 : 0;
    if (parkedLeft < walkMs + 1000)
    {
        uint32 const wait = parkedLeft ? parkedLeft : ai::LiftUntilParked(car, now, fromTop);
        SetDuration(std::min<uint32>(wait + 100, 2000));
        return true;
    }
    if (walkMs)
    {
        bot->GetMotionMaster()->MovePoint(bot->GetMapId(), car.x, car.y,
            car.z + ai::LiftCarOffset(car, now), MOVE_RUN_MODE);
        SetDuration(walkMs);
        return true;
    }

    ai->StopMoving();
    bot->m_movementInfo.SetTransportData(carObject->GetObjectGuid(), 0.0f, 0.0f, 0.0f, bot->GetOrientation(), now);
    bot->m_movementInfo.AddMovementFlag(MOVEFLAG_ONTRANSPORT);
    bot->GetMap()->PlayerRelocation(bot, car.x, car.y, car.z + ai::LiftCarOffset(car, now), bot->GetOrientation());
    bot->SendHeartBeat();

    rideLift = pick.lift;
    rideMapId = bot->GetMapId();
    rideCar = carIndex;
    waitLift = nullptr;
    rideToTop = !fromTop;
    rideCarGuid = carObject->GetObjectGuid();
    rideGoal = location;
    sPlayerbotAIConfig.logEvent(ai, "MesaLift", pick.lift->name, std::string(fromTop ? "board-down:" : "board-up:") +
        std::to_string(carIndex) + ":" + LiftLogPos(WorldPosition(bot)) + ">" + LiftLogPos(location));
    SetDuration(std::min<uint32>(ai::LiftUntilParked(car, now, rideToTop) + 100, 2000));
    return true;
}

// One tick of a ride: follow the car, step off once it parks at the other
// level. A ride cut short (teleport, death, the transport mark cleared)
// leaves the car at its nearest level.
bool MoveToTravelTargetAction::ContinueMesaLiftRide()
{
    ai::LiftCar const& car = rideLift->cars[rideCar];
    uint32 const now = WorldTimer::getMSTime();
    float const carZ = car.z + ai::LiftCarOffset(car, now);
    bool const arrived = ai::LiftParkedLeft(car, now, rideToTop) > 0;
    bool const onCar = bot->m_movementInfo.GetTransportGuid() == rideCarGuid;
    bool const placeable = bot->IsInWorld() && !bot->IsBeingTeleported() && bot->GetMapId() == rideMapId;
    bool const abort = !onCar || !placeable || !bot->IsAlive();

    noProgressArmed = false;
    if (!arrived && !abort)
    {
        bot->GetMap()->PlayerRelocation(bot, car.x, car.y, carZ, bot->GetOrientation());
        SetDuration(std::min<uint32>(ai::LiftUntilParked(car, now, rideToTop) + 100, 2000));
        return true;
    }

    bool const top = arrived ? rideToTop :
        fabs(carZ - ai::LiftLevelZ(car, true)) < fabs(carZ - ai::LiftLevelZ(car, false));
    if (onCar)
    {
        bot->m_movementInfo.ClearTransportData();
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
    }
    if (placeable)
    {
        // Off the car where it stands, then walk onto the station floor.
        float const levelZ = ai::LiftLevelZ(car, top);
        bot->GetMap()->PlayerRelocation(bot, car.x, car.y, levelZ, bot->GetOrientation());
        bot->SendHeartBeat();
        WorldPosition off(bot->GetMapId(), car.x, car.y, levelZ);
        if (off.isMmapLoaded(bot->GetInstanceId()) && off.ClosestCorrectPoint(8.0f, 6.0f, bot->GetInstanceId()))
        {
            float const step = std::hypot(off.getX() - car.x, off.getY() - car.y);
            bot->GetMotionMaster()->MovePoint(off.GetMapId(), off.getX(), off.getY(), off.getZ(), MOVE_RUN_MODE);
            SetDuration(uint32(step / std::max(bot->GetSpeed(MOVE_RUN), 1.0f) * 1000.0f) + 300);
        }
    }
    sPlayerbotAIConfig.logEvent(ai, "MesaLift", rideLift->name, std::string(arrived ? (top ? "arrived-up:" : "arrived-down:") : "aborted:") +
        LiftLogPos(WorldPosition(bot)) + ">" + LiftLogPos(rideGoal));
    rideLift = nullptr;
    return true;
}


// One arrived guild-order trip on the auction leg: an auctioneer in reach
// hands the leg to the buy action (the streak clears), any other arrival
// counts, and the leg moves down the ladder at the visit limit.
static void CountGuildOrderAuctionArrival(PlayerbotAI* ai, Player* bot)
{
    using namespace TortoiseBots::GuildOrders;
    std::optional<OrderRecord> record = Find(bot->GetGUIDLow());
    if (!record || record->state != OrderState::Working || record->supplyStep != SupplyStep::AuctionHouse)
        return;
    switch (ClassifyAuctionArrival(GuildOrderAuctioneerNear(ai, bot) != nullptr, record->arrivedAuctionVisits,
        sPlayerbotAIConfig.guildOrderAuctionVisits))
    {
        case AuctionArrival::Reset:
            NoteArrivedAuctionVisit(bot->GetGUIDLow(), true);
            break;
        case AuctionArrival::Count:
            NoteArrivedAuctionVisit(bot->GetGUIDLow(), false);
            break;
        case AuctionArrival::CountAndExit:
            NoteArrivedAuctionVisit(bot->GetGUIDLow(), false);
            AdvanceGuildOrderAfterAuction(ai, bot, *record, OrderReason::NoAuctioneer);
            break;
    }
}

bool MoveToTravelTargetAction::Execute(Event& event)
{
    if (rideLift)
        return ContinueMesaLiftRide();

    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
    {
        ai->TellDebug(ai->GetMaster(), "The target is ready to travel start now.", "debug travel");
        target->SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
    }

    TravelStatus const statusBeforeCheck = target->GetStatus();
    target->CheckStatus();
    // A guild-order trip the walk itself finished (the destination radius
    // beat the NPC lookup: the auctioneer is out of arrival range,
    // despawned or behind geometry) still counts as an arrival. Once per
    // trip: only the tick that flips it to WORK counts.
    if (statusBeforeCheck == TravelStatus::TRAVEL_STATUS_TRAVEL &&
        target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK &&
        AI_VALUE2(std::string, "manual string", "future travel purpose") == "guild order")
        CountGuildOrderAuctionArrival(ai, bot);

    if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return true;

    WorldPosition botLocation(bot);
    WorldPosition location = *target->getPosition();

    if (LeaveDeeprunTram(ai, bot, location))
        return true;

    // Decide-once flight plan (items 4-5): when the stored plan names a
    // flight, steer to the flight master as the intermediate move target
    // until inside interaction range, then board. The walk below aims at the
    // (x, y, z, mapId) locals, which the staging overwrites with the master.
    float x = location.getX();
    float y = location.getY();
    float z = location.getZ();
    float mapId = location.GetMapId();
    uint32 planFrom = 0;
    uint32 planTo = 0;
    // Re-read after a possible clear above: a stale plan drops out here
    // and the walk below targets the final destination.
    bool hasFlightPlan = ReadFlightPlan(ai, planFrom, planTo);
    if (hasFlightPlan)
    {
        if (TryBoardFlightToTarget(ai, bot, target, botLocation, location))
            return true;

        TaxiNodesEntry const* stageEntry = sObjectMgr.GetTaxiNodeEntry(planFrom);
        if (!stageEntry || stageEntry->map_id != botLocation.GetMapId() ||
            !bot->GetTaxi().IsTaximaskNodeKnown(planFrom))
        {
            // Stale plan (unlearned node, cross-map master): drop it so the
            // walk below resumes toward the final destination.
            ClearFlightPlan(ai);
        }
        else
        {
            x = stageEntry->x;
            y = stageEntry->y;
            z = stageEntry->z;
            mapId = stageEntry->map_id;
        }
        hasFlightPlan = ReadFlightPlan(ai, planFrom, planTo);
    }

    bool liftStaged = false;
    if (!hasFlightPlan && TryMesaLift(location, x, y, z, liftStaged))
        return true;

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

            // Waiting for a group member stands still by design: disarm the
            // no-displacement watchdog so the first tick after the wait does
            // not read the wait itself as a stuck move.
            noProgressArmed = false;

            return true;
        }
    }

    std::string const purpose = AI_VALUE2(std::string, "manual string", "future travel purpose");

    // A hand-in trip that has come within range of a taker the navmesh cannot
    // reach is settled here, before the bot walks another lap around it.
    if (TrySettleUnreachableHandIn(target, purpose))
        return false;

    // A service NPC already in reach counts as arrived, even when the walk
    // point below sits inside geometry. Runs before the jitter so the
    // arrival verdict never depends on the re-rolled point. A guild-order
    // arrival counts on the auction leg unless an auctioneer is in reach:
    // reaching the point is not reaching the auctioneer.
    if (!hasFlightPlan && CheckServiceArrival(target, purpose))
    {
        if (purpose == "guild order")
            CountGuildOrderAuctionArrival(ai, bot);
        return true;
    }

    // With a flight plan or a lift ride the bot walks to the master or the
    // lift station (x/y/z/mapId staged above), so the arrival jitter below
    // must not re-aim at the final destination.
    if (!hasFlightPlan && !liftStaged && botLocation.GetMapId() == location.GetMapId() && botLocation.sqDistance2d(location) < 10000.0f)
    {
        float maxDistance = target->GetDestination()->GetRadiusMin();

        // One stable approach per (bot, destination) pair (donor
        // mod-playerbots MoveToTravelTargetAction, same for every purpose):
        // re-rolling the walk point every re-entry oscillates between
        // points and never converges when one lands inside geometry - or
        // walks the bot back and forth across a platform edge (Dolanaar
        // tree-platform env deaths, Oct 2026 pool). Service trips keep
        // their tested helper; every other purpose gets the same shape.
        float dx = 0.0f, dy = 0.0f;
        ServiceTripStableOffset(bot->GetGUIDLow(), target->GetEntry(),
            location.getX(), location.getY(), maxDistance, dx, dy);
        x += dx;
        y += dy;

        // Land the walk point on the mesh, not inside the counter or above
        // the floor: the NPC's own z can sit above unmeshed space
        // (Stormwind upper floors). A snapped point keeps the stable x/y
        // approach; a failed snap keeps the raw point, exactly as before.
        WorldPosition walkPoint(mapId, x, y, z);
        if (walkPoint.isMmapLoaded(bot->GetInstanceId()) &&
            walkPoint.ClosestCorrectPoint(maxDistance, maxDistance, bot->GetInstanceId()) &&
            fabs(walkPoint.getZ() - z) <= maxDistance)
        {
            x = walkPoint.getX();
            y = walkPoint.getY();
            z = walkPoint.getZ();
        }

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

    // A step toward the target (TryStepTowardTarget) is still walking: let it finish
    // instead of clearing it with a move that already found no route.
    if (bot->IsMoving() && AI_VALUE2(time_t, "manual time", "travel step until") > time(0))
        return true;

    TravelNodeMap::LastRouteFail().clear();
    bool canMove = MoveTo(mapId, x, y, z, false, false);

    // A move that reports success while the bot does not displace (a spline
    // launched into geometry, a collapsed shortcut) fills the failure budget
    // like a refused move: each phantom success used to decay it instead, so
    // the 6-fail drop below never fired and the bot stood until the TRAVEL
    // timeout. The drop, the purpose park and the kind blacklist then engage
    // exactly as for a refused move; nothing is re-armed.
    bool const noProgress = canMove && TravelMoveMadeNoProgress();

    // A pool bot's hand-in trip keeps its move budget while it is getting
    // closer: 5 yd nearer the taker than ever before on this trip clears the
    // failures so far (HandInTripProgressed, donor MoveFarTo). Only a walk
    // that stops closing the gap reaches the drop and the 5-min quest park.
    if (sRandomBotFacade.IsFreeBot(bot) && !ai->HasRealPlayerMaster() &&
        HandInTakerDestination(target, purpose) &&
        HandInTripProgressed(target->HandInBestDistance(), botLocation.distance(location)))
        target->SetRetry(true, 0);

    if (!canMove || noProgress)
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
        // from the CSV. A second colon carries which return-false site fired
        // in MoveTo2/DispatchMovement (LastMovement::moveFailReason, stamped
        // right before each `return false`), so a "complete" probe over a
        // clipped route is separable from a dispatch that truly reached the
        // goal and failed. A third colon carries the bot/target height pair
        // (botZ>targetZ, whole yards), so an above-the-mesh trap reads
        // straight off the row. Parsers reading `dist:pathtag` keep working:
        // the reason and the heights are only ever appended after the tag.
        if (target->GetRetryCount(true) == 2)
        {
            std::string failDetail = std::to_string((int32)botLocation.distance(location));
            failDetail += ":";
            // The navmesh verdict for the blacklist below: only a same-map
            // probe ran on the mesh at all (cross-map is never probed, an
            // unloaded tile reports not-using-path and has nothing to say).
            std::string pathTag;
            if (location.GetMapId() != bot->GetMapId())
                pathTag = "crossmap";
            else
            {
                PathFinder probe(bot);
                WorldPosition::ExcludeSteepSlopes(probe);
                probe.calculate(location.getX(), location.getY(), location.getZ(), false);
                pathTag = TravelMoveFailPathTag((uint32_t)probe.getPathType());
            }
            // A guild-order farm trip the mesh cannot reach says so about that
            // target point: the planner leaves the zone out once another point
            // in it failed too, so one hole does not cost the zone's other
            // farm spots.
            if (purpose == "guild order" && TortoiseBots::GuildOrders::TravelPathUnreachable(pathTag))
            {
                AreaTableEntry const* area = location.GetArea();
                uint32_t const zoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
                if (zoneId)
                    TortoiseBots::GuildOrders::NoteFarmZoneTargetFailed(bot->GetGUIDLow(), zoneId,
                        location.GetMapId(), location.getX(), location.getY(), location.getZ());
            }
            bool const noPath = pathTag == "nopath";
            if (noPath && target->GetDestination() &&
                ai::PurposeRemembersUnreachablePoints(target->GetDestination()->GetPurpose() == TravelDestinationPurpose::GatherFishing,
                    target->GetDestination()->GetPurpose() == TravelDestinationPurpose::GenericRpg))
                sTravelMgr.NoteUnreachablePoint(location);
            failDetail += pathTag;
            failDetail += ":";
            failDetail += MoveFailReasonName(AI_VALUE(LastMovement&, "last movement").moveFailReason);
            failDetail += ":";
            failDetail += std::to_string((int32)botLocation.getZ()) + ">" + std::to_string((int32)location.getZ());
            // Fifth field: why the node route was empty (TravelNodeMap::getRoute), when one was asked for.
            if (!TravelNodeMap::LastRouteFail().empty())
                failDetail += ":" + TravelNodeMap::LastRouteFail();
            sPlayerbotAIConfig.logEvent(ai, "TravelMoveFailed", purpose, failDetail);

            // Give up the kind like a wedged combat target: a mesh-proven
            // hole to a grind spot blacklists the creature kind for five
            // minutes (the reach-action window), so the re-pick after the
            // coming drop walks a different kind instead of the same spot.
            // Without it the bot drops, re-picks the identical target and
            // fires "move stuck" resets in place indefinitely.
            if (TravelMoveFailBlacklistsKind(!ai->HasRealPlayerMaster() && sRandomBotFacade.IsFreeBot(bot),
                dynamic_cast<GrindTravelDestination*>(target->GetDestination()) != nullptr,
                noPath, target->GetEntry()))
            {
                AiObjectContext* context = ai->GetAiObjectContext();
                uint32 const nowMs = WorldTimer::getMSTime();
                context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[(uint32)target->GetEntry()] =
                    nowMs + 5 * MINUTE * IN_MILLISECONDS;
                ai->TellDebug(ai->GetMaster(), "Giving up on this hunting ground - no path there.", "debug travel");
            }


            // A bot that cannot walk to any route node is trapped the same way:
            // islands with no nodes (Lapidis Isle, live 2026-10-09) failed every
            // off-island target as "not-using-path" + startwalk at the shore and
            // never counted toward the trap streak.
            std::string const& routeFail = TravelNodeMap::LastRouteFail();
            bool const offGraph = routeFail == "startwalk" || routeFail == "nostartnode";
            if (TryRescueNoPathTrap(noPath || offGraph, offGraph, purpose))
                return false;
        }

        // A step toward the target (or a hop back onto the mesh) replaces the
        // drop. The odd count keeps the first-failure row from repeating.
        if (!canMove && TryStepTowardTarget(location))
        {
            target->SetRetry(true, 1);
            return true;
        }

        // A failed move toward a hand-in taker is one no-progress episode for that
        // quest, the same episode a re-picked taker counts (ChooseTravelTargetAction).
        // Settling on it pays the quest, so nothing is left to walk to and the next
        // tick may pick again.
        if (QuestTravelDestination* destination = HandInTakerDestination(target, purpose))
            CountHandInNoProgress(ai, destination->GetQuestId(), destination->GetEntry());

        if (TrySettleUnreachableHandIn(target, purpose))
            return false;

        // A stuck service trip teleports to its destination instead of
        // dropping + parking (owner request): unwatched, same map, random
        // masterless pool bot, once per 30 min. A refused or failed rescue
        // falls through to the ordinary drop below.
        if (TryRescueServiceTrip(target, purpose))
            return true;

        // Any other stuck trip of a masterless pool bot teleports to
        // its destination instead of dropping (AiPlayerbot.StuckTravelTeleport).
        if (TryRescueStuckTravel(target, purpose))
            return true;

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
            // A workshop leg remembers its own point: the purpose park below
            // only delays the re-pick, which then offers the same forge
            // again. Inside smelt range the bot crafts from where it stands,
            // so only a still-distant drop writes the point off.
            bool const orderLeg = purpose == "order focus" || purpose == "order vendor";
            bool const orderFocusTrip = purpose == "order focus";
            if (ai::FocusDropMarksSpot(orderFocusTrip, botLocation.distance(location)))
                ai->MarkUnreachableFocusSpot(location.GetMapId(), location.getX(), location.getY(),
                    WorldTimer::getMSTime());
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
            // A dropped workshop leg re-picks after a minute: the order makes
            // no progress while parked, so the next forge must come soon.
            // A stuck drop is not an arrival: the bot never reached the
            // destination, so the auction-leg visit count stays untouched.
            // Real arrivals count (and exit) in CountGuildOrderAuctionArrival.
            std::string const parkKey = TravelPurposeParkKey(purpose);
            SET_AI_VALUE2(bool, "no active travel destinations", parkKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + parkKey,
                time(0) + (orderFocusTrip ? ai::ORDER_FOCUS_PARK_SECONDS : orderLeg ? TortoiseBots::GuildOrders::kOrderLegParkSeconds : 5 * MINUTE));
            if (DropParkClearsTrainerMoneyPark(parkKey))
                SET_AI_VALUE2(bool, "manual bool", "trainer park needs money", false);
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
    if (rideLift) // a bot on a lift car rides it to the end
        return true;

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

    // Leg chaining: a turn that lands while the current leg's spline is
    // nearly finished stays useful so Execute dispatches the NEXT leg in the
    // same turn instead of waiting for the leg to end (~3 s still on a busy
    // realm). Pure walk legs only: the helper refuses combat, flight paths,
    // mounts, swimming, transports, dungeon and group-wait moves.
    bool const legChainDue = bot->IsMoving() && TravelLegChainDue(ai, bot);
    if (MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 10)
        if (bot->IsMoving() && !legChainDue)
            return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    // Follow bots stay with their master instead of walking order legs, but a
    // masterless bot's wander is free movement, not a leash: it must walk them.
    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || (ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) && ai->HasRealPlayerMaster()))
    {
        auto conditions = travelTarget->GetConditions();
        for (auto& cond : conditions)
        {
            if (cond == "has guild travel order" ||
                cond == "has guild order delivery" ||
                cond == "should travel named::order focus" ||
                cond == "should travel named::order vendor")
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
