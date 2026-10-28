#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "playerbot/GroupMembers.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/values/GuildValues.h"
#include "SocialMgr.h"

using namespace ai;

// Phase 2 guarded spatial-scan cadence. All guards are O(1) field reads or a
// cheap squared-distance walk over the handful of real network players; the
// expensive Cell::VisitAllObjects grid scan they gate is ~60% of tick CPU.
// True idle (taxi flight, rested sanctuary) skips the scan entirely by
// returning an empty candidate set from PossibleTargetsValue::Calculate.
namespace
{
constexpr uint32 kGrindScanIntervalMs = 1000;
constexpr uint32 kGrindScanMaxStaleMs = 1500;
constexpr float kHumanWakeDistanceYd = 50.0f;
bool HasNetworkHumanNearby(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return false;
    // Cheap O(humans) squared-distance walk; the grid scan it gates is O(cells).
    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (!session || !session->HasNetworkTransport())
            continue;
        Player* player = session->GetPlayer();
        if (!player || player == bot || !player->IsInWorld())
            continue;
        if (player->GetMapId() != bot->GetMapId())
            continue;
        float dist2d = player->GetDistance2d(bot);
        if ((!player->IsGameMaster() || player->IsGMVisible()) && dist2d <= kHumanWakeDistanceYd)
            return true;
    }
    return false;
}
} // namespace

/*
enum ActivityType
{
    GRIND_ACTIVITY = 1,
    RPG_ACTIVITY = 2,
    TRAVEL_ACTIVITY = 3,
    OUT_OF_PARTY_ACTIVITY = 4,
    PACKET_ACTIVITY = 5,
    DETAILED_MOVE_ACTIVITY = 6,
    PARTY_ACTIVITY = 7
    ALL_ACTIVITY = 8
};

   General function to check if a bot is allowed to be active or not.
   This function should be checked first before doing heavy-workload.


*/

ActivePiorityType PlayerbotAI::GetPriorityType()
{
    //First priority - priorities disabled or has player master. Always active.
    if (sPlayerbotAIConfig.disableActivityPriorities || HasRealPlayerMaster())
        return ActivePiorityType::HAS_REAL_PLAYER_MASTER;

    //Self bot in a group with a bot master.
    if (IsRealPlayer())
        return ActivePiorityType::IS_REAL_PLAYER;

    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {

            if (!member || !member->IsInWorld())
                continue;

            if (member == bot)
                continue;

            if (!PlayerbotAIStorage::Instance().GetAI(member) || (PlayerbotAIStorage::Instance().GetAI(member) && PlayerbotAIStorage::Instance().GetAI(member)->HasRealPlayerMaster()))
                return ActivePiorityType::IN_GROUP_WITH_REAL_PLAYER;
        }
    }

    if (bot->IsBeingTeleported()) //We might end up in a bg so stay active.
        return ActivePiorityType::IN_BATTLEGROUND;

    if (WorldPosition(bot).isBg())
        return ActivePiorityType::IN_BATTLEGROUND;

    AiObjectContext* context = GetAiObjectContext();

    if (AI_VALUE2(bool, "manual bool", "is running test"))
        return ActivePiorityType::IS_RUNNING_TEST;

    if (!WorldPosition(bot).isOverworld())
    {
        if (!sPlayerbotAIConfig.enableMinimalMove)
            return ActivePiorityType::IN_INSTANCE;
        else
        {
            LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
            if (lastMove.lastPath.empty())
                return ActivePiorityType::IN_INSTANCE;
        }
    }

    if (HasPlayerNearby())
        return ActivePiorityType::VISIBLE_FOR_PLAYER;

    if (sPlayerbotAIConfig.guildOrderAlwaysActive && bot->IsInWorld() && bot->GetGuildId())
    {
        AiObjectContext* context = GetAiObjectContext();
        GuildOrder guildOrder = AI_VALUE(GuildOrder, "guild order");
        if (guildOrder.IsValid())
            return ActivePiorityType::IS_ALWAYS_ACTIVE;
    }

    if (sServerFacade.IsInCombat(bot))
        return ActivePiorityType::IN_COMBAT;

    if (HasPlayerNearby(WorldPosition(bot).getVisibilityDistance() + sPlayerbotAIConfig.reactDistance))
        return ActivePiorityType::NEARBY_PLAYER;

    if (sPlayerbotAIConfig.IsFreeAltBot(bot) || HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
        return ActivePiorityType::IS_ALWAYS_ACTIVE;

    if (bot->InBattleGroundQueue())
        return ActivePiorityType::IN_BG_QUEUE;

    if (sPlayerbotAIConfig.enableMinimalMove)
    {
        AiObjectContext* context = GetAiObjectContext();
        LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
        if (lastMove.lastPath.empty() && !urand(0, 5))
            return ActivePiorityType::NO_PATH;
    }

    // If the native random population is empty, slow down continents without
    // random-bot activity. GetPlayers() is a compatibility view, not a
    // network-player registry.
    //This means we first disable bots in a different continent/area.
    if (sRandomBotFacade.GetPlayers().empty())
        return ActivePiorityType::IN_EMPTY_SERVER;

    // Friends are real human social links, not random bots. The old loop
    // scanned every headless random bot (N bots x N ticks) even though
    // headless bots never befriend anyone. Only network-transport players
    // can hold a real friend entry for this bot.
    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (!session || !session->HasNetworkTransport())
            continue;
        Player* player = session->GetPlayer();
        if (!player || player == bot || !player->IsInWorld())
            continue;
        PlayerSocial* social = player->GetSocial();
        if (social && social->HasFriend(bot->getObjectGuid()))
            return ActivePiorityType::PLAYER_FRIEND;
    }

    // real guild always active if member+
    if (IsInRealGuild())
        return ActivePiorityType::PLAYER_GUILD;

    if (bot->IsBeingTeleported() || !bot->IsInWorld() || !bot->GetMap()->HavePlayers())
        return ActivePiorityType::IN_INACTIVE_MAP;

    return ActivePiorityType::IN_ACTIVE_AREA;
}

//Returns the lower and upper bracket for bots to be active.
//Ie. 10,20 means all bots in this bracket will be inactive below 10% activityMod, all bots in this bracket will be active above 20% activityMod and scale between those values.
std::pair<uint32, uint32> PlayerbotAI::GetPriorityBracket(ActivePiorityType type)
{
    switch (type)
    {
    case ActivePiorityType::HAS_REAL_PLAYER_MASTER:
    case ActivePiorityType::IS_REAL_PLAYER:
    case ActivePiorityType::IN_GROUP_WITH_REAL_PLAYER:
    case ActivePiorityType::VISIBLE_FOR_PLAYER:
    case ActivePiorityType::IN_BATTLEGROUND:
    case ActivePiorityType::IS_RUNNING_TEST:
        return { 0,0 };
    case ActivePiorityType::IN_INSTANCE:
        return { 0,5 };
    case ActivePiorityType::IS_ALWAYS_ACTIVE:
        return { 0,0 };
    case ActivePiorityType::IN_COMBAT:
    {
        if (sPlayerbotAIConfig.limitCombatActivity)
            return { 99,100 };

        return { 0,10 };
    }
    case ActivePiorityType::IN_BG_QUEUE:
        return { 0,20 };
    case ActivePiorityType::NEARBY_PLAYER:
        return { 0,40 };
    case ActivePiorityType::PLAYER_FRIEND:
    case ActivePiorityType::PLAYER_GUILD:
        return { 0,50 };
    case ActivePiorityType::NO_PATH:
        return { 50, 99};
    case ActivePiorityType::IN_ACTIVE_AREA:
    case ActivePiorityType::IN_EMPTY_SERVER:
        return { 50,100 }; //Note lower 100 means multiply by activity percentage.
    case ActivePiorityType::IN_ACTIVE_MAP:
        return { 70,100 };
    case ActivePiorityType::IN_INACTIVE_MAP:
        return { 80,100 };
    default :
        return { 90, 100 };
    }

    return { 90, 100 };
}

bool PlayerbotAI::AllowActive(ActivityType activityType)
{
    ActivePiorityType type = GetPriorityType();

    if (activityType == DETAILED_MOVE_ACTIVITY)
    {
        switch (type)
        {
        case ActivePiorityType::HAS_REAL_PLAYER_MASTER:
        case ActivePiorityType::IS_REAL_PLAYER:
        case ActivePiorityType::IN_GROUP_WITH_REAL_PLAYER:
        case ActivePiorityType::IN_INSTANCE:
        case ActivePiorityType::VISIBLE_FOR_PLAYER:
        case ActivePiorityType::IN_COMBAT:
        case ActivePiorityType::NEARBY_PLAYER:
        case ActivePiorityType::IS_RUNNING_TEST:
            return true;
            break;
        case ActivePiorityType::IS_ALWAYS_ACTIVE:
        case ActivePiorityType::IN_BG_QUEUE:
        case ActivePiorityType::PLAYER_FRIEND:
        case ActivePiorityType::PLAYER_GUILD:
        case ActivePiorityType::NO_PATH:
        case ActivePiorityType::IN_ACTIVE_AREA:
        case ActivePiorityType::IN_EMPTY_SERVER:
        case ActivePiorityType::IN_ACTIVE_MAP:
        case ActivePiorityType::IN_INACTIVE_MAP:
        default:
            break;
        }
    }
    else if (activityType == REACT_ACTIVITY)
    {
        switch (type)
        {
        case ActivePiorityType::HAS_REAL_PLAYER_MASTER:
        case ActivePiorityType::IS_REAL_PLAYER:
        case ActivePiorityType::IN_GROUP_WITH_REAL_PLAYER:
        case ActivePiorityType::IN_INSTANCE:
        case ActivePiorityType::IS_ALWAYS_ACTIVE:
        case ActivePiorityType::IS_RUNNING_TEST:
        case ActivePiorityType::IN_COMBAT:
            return true;
        case ActivePiorityType::VISIBLE_FOR_PLAYER:
            if (sPlayerbotAIConfig.forceActiveWhenNearPlayer)
                return true;
            break;
        case ActivePiorityType::NEARBY_PLAYER:
        case ActivePiorityType::IN_BG_QUEUE:
        case ActivePiorityType::PLAYER_FRIEND:
        case ActivePiorityType::PLAYER_GUILD:
        case ActivePiorityType::NO_PATH:
        case ActivePiorityType::IN_ACTIVE_AREA:
        case ActivePiorityType::IN_EMPTY_SERVER:
        case ActivePiorityType::IN_ACTIVE_MAP:
        case ActivePiorityType::IN_INACTIVE_MAP:
        default:
            break;
        }
    }

    std::pair<uint8, uint8> priorityBracket = GetPriorityBracket(type);

    constexpr float activityPercentage = 100.0f;

    if (!priorityBracket.second) //No scaling
        return true;

    if (priorityBracket.first >= activityPercentage)
        return false;
    if (priorityBracket.second <= activityPercentage && priorityBracket.second < 100)
        return true;

    float activePerc = (activityPercentage - priorityBracket.first) / (priorityBracket.second - priorityBracket.first);

    activePerc *= (priorityBracket.second == 100) ? sPlayerbotAIConfig.botActiveAlone : 100;

    uint32 ActivityNumber = GetFixedBotNumber(BotTypeNumber::ACTIVITY_TYPE_NUMBER, 100, activePerc * 0.01f); //The last number if the amount it cycles per min. Currently set to 1% of the active bots.

    return ActivityNumber <= (activePerc);           //The given percentage of bots should be active and rotate 1% of those active bots each minute.
}


bool PlayerbotAI::IsSpatialScanIdle() const
{
    if (!bot)
        return true;
    if (bot->IsTaxiFlying())
        return true;
    // Rested in a protected zone (inn/capital): stationary regen, no threats.
    // Sanctuary flag covers city/inn rest areas; RESTING without movement
    // covers drinking/eating recovery elsewhere safe.
    if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) &&
        (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY) || !sServerFacade.isMoving(bot)))
        return true;
    return false;
}

bool PlayerbotAI::ShouldReuseSpatialScan()
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return true;
    // Dead bots (corpse run, release, revive) must never be throttled.
    if (!bot->IsAlive())
        return false;
    // Combat runs at full 100ms rate.
    if (sServerFacade.IsInCombat(bot))
        return false;
    // Sudden damage / mana spend wakes the scan immediately. Scalar field
    // reads only; the grid scan they gate is the ~60% tick cost.
    uint32 hp = bot->GetHealth();
    uint32 mana = bot->GetPower(bot->GetPowerType());
    float hpPct = bot->GetMaxHealth() ? (hp * 100.0f / bot->GetMaxHealth()) : 100.0f;
    if (hpPct < 80.0f || (m_lastMana && mana < m_lastMana))
    {
        m_lastMana = mana;
        return false;
    }
    m_lastMana = mana;
    uint32 nowMs = WorldTimer::getMSTime();
    // Post-revive / post-teleport grace: full rate while a grace window is open
    // (revive 60 s, teleport 15 s). Future timestamps mean grace is active
    // (wraparound-safe: diff grace->now is huge).
    if (WorldTimer::getMSTimeDiff(m_reviveGraceUntilMs, nowMs) > (UINT32_MAX / 2) ||
        WorldTimer::getMSTimeDiff(m_teleportGraceUntilMs, nowMs) > (UINT32_MAX / 2))
        return false;
    // Human in sight: full rate.
    if (HasNetworkHumanNearby(bot))
        return false;
    // Max-stale fallback: never older than 1.5s. First scan always runs.
    uint32 staleMs = WorldTimer::getMSTimeDiff(m_lastSpatialScanMs, nowMs);
    if (!m_lastSpatialScanMs || staleMs >= kGrindScanMaxStaleMs)
        return false;
    // Staggered 1s cadence: per-bot phase from the GUID spreads 1000 bots
    // across the second so they never all scan on the same tick boundary.
    uint32 phaseMs = bot->GetGUIDLow() % kGrindScanIntervalMs;
    return staleMs < kGrindScanIntervalMs &&
        ((nowMs + phaseMs) % kGrindScanIntervalMs) >= 100;
}

bool PlayerbotAI::HasPlayerRelation()
{
    if (HasRealPlayerMaster())
        return true;

    if (IsInRealGuild())
        return true;

    if (IsPlayerFriend())
        return true;

    if (!sRandomBotFacade.IsRandomBot(bot))
        return true;

    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (!session || !session->HasNetworkTransport())
            continue;
        Player* peer = session->GetPlayer();
        if (!peer || peer == bot || !peer->IsInWorld())
            continue;

        PlayerSocial* social = peer->GetSocial();
        if (social && social->HasFriend(bot->getObjectGuid()))
        {
            SetPlayerFriend(true);
            return true;
        }
    }

    return false;
}

void PlayerbotAI::QueueChatResponse(uint32 msgType, ObjectGuid guid1, ObjectGuid guid2, std::string message, std::string chanName, std::string name, bool noDelay)
{
    std::scoped_lock lock(chatRepliesMutex);
    chatReplies.push(ChatQueuedReply(msgType, guid1.GetCounter(), guid2.GetCounter(), message, chanName, name, time(0) + (noDelay ? 0 : urand(inCombat ? 15 : 10, inCombat ? 30 : 20))));
}
