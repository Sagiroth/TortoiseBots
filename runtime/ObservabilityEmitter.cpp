// Winsock has to be the first thing this translation unit includes. windows.h - which
// several core headers below pull in transitively - defaults to winsock1 if it gets there
// first, and winsock2.h then collides with it (WinSock.h already declared errors). Nothing
// here happened to trip that only because DatabaseMysql.h currently includes winsock2.h
// itself before any of these; that is an accident of its own include order, not a guarantee.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "ObservabilityEmitter.h"

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"
// (kept: sServerFacade.IsAlive/IsInCombat/IsHostileTo used below)
#include "../ai/playerbot/ServerFacade.h"
#include "World.h"
#include "SystemConfig.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "../host/ModuleLog.h"
#include "../host/ModuleVersion.h"
#include "Timer.h"
#include "MotionMaster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <set>
#include <sstream>
#include <iomanip>

namespace TortoiseBots {

namespace {

// Datagram schema version. The Go daemon ignores datagrams it cannot parse;
// this is bumped when the wire format changes incompatibly.
constexpr int kProtocolVersion = 8;

// Snapshot cadence and batching. Datagrams are kept well under the loopback
// MTU so a large roster arrives as several unpredictable chunks; the receiver
// assembles them per Seq.
constexpr uint32 kSnapshotIntervalMs = 2000;
// 10 bots per 2 s snapshot refreshes a 500-bot pool every ~100 s.
constexpr size_t kArmoryBotsPerSnapshot = 10;
constexpr size_t kBatchSize = 25;

// Retention bounds. All three tables are pruned on every snapshot, so a long
// uptime with heavy bot churn cannot grow them without limit.
constexpr uint32 kBotTrackingTtlMs = 60000;
constexpr uint32 kActionFailureTtlMs = 30000;
constexpr uint32 kAnomalyCooldownTtlMs = 600000;
constexpr size_t kMaxTrackedBots = 5000;
constexpr size_t kMaxActionFailures = 4096;
constexpr size_t kMaxAnomalyCooldowns = 8192;

// Activity stream bounds: at most this many rows wait for the next snapshot
// flush (overflow drops the batch), chunked this many per datagram to stay
// well under the loopback MTU.
constexpr size_t kMaxPendingEvents = 2048;
constexpr size_t kEventBatchSize = 40;

// Per-bot/type anomaly cooldown, so a repeatedly failing bot cannot flood the
// dashboard and Prometheus counters.
constexpr uint32 kAnomalyCooldownMs = 30000;

enum MacroState : uint8
{
    STATE_COMBAT = 0,
    STATE_MOVING = 1,
    STATE_BUSY = 2,
    STATE_STALLED = 3,
    STATE_RESTING = 4,
    STATE_DEAD = 5,
    STATE_IDLE = 6,
};

char const* MacroStateName(uint8 state)
{
    switch (state)
    {
        case STATE_COMBAT:  return "combat";
        case STATE_MOVING:  return "moving";
        case STATE_BUSY:    return "busy";
        case STATE_STALLED: return "stalled";
        case STATE_RESTING: return "resting";
        case STATE_DEAD:    return "dead";
        default:            return "idle";
    }
}

enum AnomalyTypeId : uint8
{
    ANOMALY_UNKNOWN = 0,
    ANOMALY_STUCK = 1,
    ANOMALY_ACTION_LOOP = 2,
    ANOMALY_UNREACHABLE = 3,
};

uint8 AnomalyTypeIdFromName(std::string const& type)
{
    if (type == "STUCK") return ANOMALY_STUCK;
    if (type == "ACTION_LOOP") return ANOMALY_ACTION_LOOP;
    if (type == "UNREACHABLE_TARGET") return ANOMALY_UNREACHABLE;
    return ANOMALY_UNKNOWN;
}

// Activity whitelist: the bot_events.csv rows the dashboard's per-bot activity
// rollup consumes. Everything else (travel churn, buffs, evade probes) stays
// out of the UDP stream.
// QuestUpdateCompleteAction is packet-driven and never fires for a headless bot
// session, so the quest-complete counters ride on QuestCompleted, which
// BotPlayerAdapter emits from the core's own quest-complete hook.
bool IsActivityEvent(std::string const& event)
{
    static std::set<std::string> const whitelist = {
        // quests
        "QuestRewarded", "AcceptQuestAction", "AcceptQuestShareAction",
        "TalkToQuestGiverAction", "QuestUpdateCompleteAction", "QuestCompleted", "QuestDropped",
        // loot & money
        "StoreLootAction", "GatherLoot", "LootMoney",
        // vendor / trainer / repair / auction
        "SellAction", "BuyAction", "RepairAllAction", "TrainerAction", "NearbyService",
        "AhAction", "AhBidAction",
        // deaths, revives (ghost time) and give-ups
        "BotDeath", "ReviveFromCorpseAction", "ReviveFromSpiritHealerAction", "RepopAction",
        "LongStuckFallback", "ReachGiveUp",
        // explicit kill signal from XpGainAction (the CSV row cannot carry the
        // kill/non-kill XP flag)
        "Kill",
    };
    return whitelist.count(event) != 0;
}

// Events whose info2 is an item id: enriched with the prototype's quality and
// prices so the dashboard can filter loot by quality and value it.
bool IsItemEvent(std::string const& event)
{
    static std::set<std::string> const itemEvents = {
        "StoreLootAction", "GatherLoot", "SellAction", "BuyAction", "AhAction", "AhBidAction",
    };
    return itemEvents.count(event) != 0;
}

uint32 ParseItemId(std::string const& s)
{
    if (s.empty())
        return 0;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end == s.c_str() || v == 0 || v > 0x7FFFFFFF)
        return 0;
    return static_cast<uint32>(v);
}

std::string EscapeJson(std::string const& s)
{
    std::ostringstream o;
    for (char c : s)
    {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f)
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)(unsigned char)c;
        else
            o << c;
    }
    return o.str();
}

std::string GetBotClassName(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER:  return "hunter";
        case CLASS_ROGUE:   return "rogue";
        case CLASS_PRIEST:  return "priest";
        case CLASS_SHAMAN:  return "shaman";
        case CLASS_MAGE:    return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID:   return "druid";
        default:            return "unknown";
    }
}

std::string GetPowerTypeName(uint8 power)
{
    switch (power)
    {
        case POWER_MANA:      return "mana";
        case POWER_RAGE:      return "rage";
        case POWER_FOCUS:     return "focus";
        case POWER_ENERGY:    return "energy";
        case POWER_HAPPINESS: return "happiness";
        default:              return "power";
    }
}

std::string FormatStrategies(PlayerbotAI* ai)
{
    if (!ai)
        return "";
    auto list = ai->GetStrategies(ai->GetState());
    std::ostringstream ss;
    bool first = true;
    for (auto const& s : list)
    {
        if (!first)
            ss << ", ";
        ss << s;
        first = false;
    }
    return ss.str();
}

std::string GetBotRole(Player* bot, PlayerbotAI* ai)
{
    if (ai)
    {
        if (ai->GetForcedRole() == 1 || ai->HasStrategy("tank", BotState::BOT_STATE_COMBAT))
            return "tank";
        if (ai->GetForcedRole() == 2 || ai->HasStrategy("heal", BotState::BOT_STATE_COMBAT) ||
            ai->HasStrategy("healer", BotState::BOT_STATE_COMBAT))
            return "healer";
    }
    if (bot && bot->GetClass() == CLASS_PRIEST && (!ai || !ai->HasStrategy("shadow", BotState::BOT_STATE_COMBAT)))
        return "healer";
    return "dps";
}

uint8 BaseMacroState(Player* bot, PlayerbotAI* ai)
{
    if (!sServerFacade.IsAlive(bot) || (ai && ai->GetState() == BotState::BOT_STATE_DEAD))
        return STATE_DEAD;

    if (sServerFacade.IsInCombat(bot) || (ai && ai->GetState() == BotState::BOT_STATE_COMBAT))
        return STATE_COMBAT;

    if (bot->IsMoving() ||
        (bot->GetMotionMaster() &&
         (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ||
          bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
          bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)))
        return STATE_MOVING;

    if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
        return STATE_RESTING;

    // Standing still but doing something: looting, casting, sitting to
    // eat/drink, or an active travel destination being worked. Member reads
    // only; the work-target flag is refreshed at snapshot cadence.
    if (bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING))
        return STATE_BUSY;
    if (bot->IsNonMeleeSpellCasted(false))
        return STATE_BUSY;
    if (bot->GetStandState() == UNIT_STAND_STATE_SIT)
        return STATE_BUSY;

    return STATE_IDLE;
}
// Whether the bot holds an active travel destination (snapshot cadence
// only, never per tick): the AI is working something even while standing
// still (vendoring, gathering, questing). Cached value lookup.
bool HasActiveWorkTarget(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return false;
    ai::Value<ai::TravelTarget*>* travel =
        ai->GetAiObjectContext()->GetValue<ai::TravelTarget*>("travel target");
    if (!travel)
        return false;
    ai::TravelTarget* target = travel->Get();
    return target && target->IsActive();
}

// Observable activity this tick, split by kind. "progress" is real work: the
// bot moved, looted, cast, or sat to eat/drink. "churn" is the pair of
// standing-still signals that used to pass for busy on their own — a new
// last-action name, or an active travel target. Keeping them apart is what
// lets a bot standing with a target be told from one that is working.
// Member reads + one name compare; no DB, no scans, no AI-value lookup.
struct Activity
{
    bool progress = false;
    bool churn = false;
    bool any = false;
};

Activity NoteActivity(Player* bot, bool hasWorkTarget, ObservabilityEmitter::BotTrackState& track, char const* actionName)
{
    Activity act;
    float dx = bot->GetPositionX() - track.lastX;
    float dy = bot->GetPositionY() - track.lastY;
    if (dx * dx + dy * dy >= 0.25f)
        act.progress = true;
    if (bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING))
        act.progress = true;
    if (bot->IsNonMeleeSpellCasted(false))
        act.progress = true;
    if (bot->GetStandState() == UNIT_STAND_STATE_SIT)
        act.progress = true;
    if (hasWorkTarget || (actionName && *actionName && track.lastActionName != actionName))
        act.churn = true;
    act.any = act.progress || act.churn;
    if (actionName && *actionName)
        track.lastActionName = actionName;
    return act;
}

// Active travel destination for the grinding panel. Cached AI values only
// (the value object already exists; Get() returns the pointer); GetShortName/
// GetTitle are cheap string builders on the already-chosen destination. Idle
// bots contribute empty strings so the pool rollup can count not-travelling.
void FillTravelInfo(PlayerbotAI* ai, std::string& purpose, std::string& to, std::string& status, int32& dist)
{
    purpose.clear();
    to.clear();
    status.clear();
    dist = -1;
    if (!ai || !ai->GetAiObjectContext())
        return;
    ai::Value<ai::TravelTarget*>* value =
        ai->GetAiObjectContext()->GetValue<ai::TravelTarget*>("travel target");
    if (!value)
        return;
    ai::TravelTarget* target = value->Get();
    if (!target || !target->IsActive())
        return;
    ai::TravelDestination* dest = target->GetDestination();
    if (!dest)
        return;
    purpose = dest->GetShortName();
    if (purpose == "unknown" || purpose == "idle" || purpose == "none" || purpose.empty())
    {
        purpose.clear();
        to.clear();
        return;
    }
    to = dest->GetTitle();
    static char const* const statusNames[] = { "none", "prepare", "ready", "travel", "work", "cooldown", "expired" };
    uint8 const st = static_cast<uint8>(target->GetStatus());
    status = st < 7 ? statusNames[st] : "unknown";
    dist = static_cast<int32>(target->Distance(ai->GetBot()));
}

} // anonymous namespace

ObservabilityEmitter& ObservabilityEmitter::Instance()
{
    static ObservabilityEmitter instance;
    return instance;
}

ObservabilityEmitter::ObservabilityEmitter()
    : m_enabled(false)
    , m_host("127.0.0.1")
    , m_port(9195)
    , m_socketFd(kInvalidSocket)
    , m_destAddr(nullptr)
    , m_snapshotTimerMs(0)
    , m_sessionId(0)
    , m_snapshotSeq(0)
    , m_stateBucketIndex(0)
    , m_stateBucketElapsedMs(0)
{
    std::memset(m_stateWindow, 0, sizeof(m_stateWindow));
}

ObservabilityEmitter::~ObservabilityEmitter()
{
    Shutdown();
}

// Resolves an IPv4 literal or host name; returns s_addr in network order,
// or 0 when the name does not resolve (yet).
static uint32_t ResolveIPv4(std::string const& host)
{
    struct in_addr literal{};
    if (inet_pton(AF_INET, host.c_str(), &literal) == 1)
        return literal.s_addr;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return 0;
    uint32_t resolved = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return resolved;
}

void ObservabilityEmitter::RetryHostResolution(uint32 diff)
{
    if (m_resolveFuture.valid())
    {
        if (m_resolveFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        uint32_t resolved = m_resolveFuture.get();
        if (!resolved)
            return;
        {
            std::lock_guard<std::mutex> lock(m_socketMutex);
            if (m_destAddr)
                static_cast<struct sockaddr_in*>(m_destAddr)->sin_addr.s_addr = resolved;
        }
        m_hostResolved = true;
        TB_LOG_BASIC("TortoiseBots: Observability telemetry active on %s:%u", m_host.c_str(), m_port);
        return;
    }

    m_resolveRetryMs += diff;
    if (m_resolveRetryMs < kResolveRetryMs)
        return;
    m_resolveRetryMs = 0;
    // DNS can block for seconds; never on the world thread.
    m_resolveFuture = std::async(std::launch::async, ResolveIPv4, m_host);
}

void ObservabilityEmitter::Initialize()
{
    // Re-initialization must not leak the previous socket or stale state.
    Shutdown();

    m_enabled = sPlayerbotAIConfig.observability ||
                sConfig.GetBoolDefault("AiPlayerbot.Observability", false) ||
                sConfig.GetBoolDefault("TortoiseBots.Observability", false);

    if (!m_enabled)
        return;

    m_port = sPlayerbotAIConfig.observabilityPort;
    if (m_port == 0)
        m_port = sConfig.GetIntDefault("AiPlayerbot.ObservabilityPort", 9195);
    if (m_port == 0)
        m_port = 9195;

    // Unix seconds is ample resolution: a restarted server is a new session.
    m_sessionId = static_cast<uint64>(time(nullptr));

    m_host = sPlayerbotAIConfig.observabilityHost;
    if (m_host.empty())
        m_host = sConfig.GetStringDefault("AiPlayerbot.ObservabilityHost", "");
    if (m_host.empty())
    {
        if (char const* envHost = std::getenv("OBSERVABILITY_HOST"))
            m_host = envHost;
    }
    if (m_host.empty())
        m_host = "127.0.0.1";

    SocketHandle fd = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (fd == kInvalidSocket)
    {
        sLog.outError("TortoiseBots: failed to create UDP socket for Observability emitter");
        m_enabled = false;
        return;
    }

#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonBlocking);
#else
    int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
    if (flags >= 0)
        fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in* addr = new struct sockaddr_in();
    std::memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(static_cast<uint16>(m_port));

    m_hostResolved = true;
    m_resolveRetryMs = 0;
    if (uint32_t resolved = ResolveIPv4(m_host))
        addr->sin_addr.s_addr = resolved;
    else
    {
        // Keep the configured host and retry later: at boot the dashboard
        // container may not be up yet, and a one-time fallback would send
        // every datagram to the wrong place for the whole session.
        sLog.outError("TortoiseBots: Observability cannot resolve host '%s' yet; sending to 127.0.0.1 and retrying every %u s",
            m_host.c_str(), kResolveRetryMs / 1000);
        m_hostResolved = false;
        inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr);
    }

    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        m_socketFd = fd;
        m_destAddr = addr;
    }

    if (m_hostResolved)
        TB_LOG_BASIC("TortoiseBots: Observability telemetry active on %s:%u", m_host.c_str(), m_port);
}

void ObservabilityEmitter::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_socketFd != kInvalidSocket)
        {
#ifdef _WIN32
            closesocket(static_cast<SOCKET>(m_socketFd));
#else
            close(static_cast<int>(m_socketFd));
#endif
            m_socketFd = kInvalidSocket;
        }
        if (m_destAddr)
        {
            delete static_cast<struct sockaddr_in*>(m_destAddr);
            m_destAddr = nullptr;
        }
    }

    m_enabled = false;
    m_snapshotTimerMs = 0;
    m_serverInfoTimerMs = 0;
    m_snapshotSeq = 0;
    m_stateBucketIndex = 0;
    m_stateBucketElapsedMs = 0;
    std::memset(m_stateWindow, 0, sizeof(m_stateWindow));
    m_botTracking.clear();
    m_actionFailures.clear();
    m_anomalyCooldowns.clear();
    m_pendingEvents.clear();
}

bool ObservabilityEmitter::IsEnabled() const
{
    return m_enabled && m_socketFd != kInvalidSocket;
}

void ObservabilityEmitter::SetExternalRosterProvider(std::function<void(std::vector<Player*>&)> provider)
{
    m_externalRosterProvider = std::move(provider);
}

void ObservabilityEmitter::SendDatagram(std::string const& payload)
{
    if (!IsEnabled())
        return;

    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_socketFd == kInvalidSocket || !m_destAddr)
        return;

#ifdef _WIN32
    // No MSG_DONTWAIT on Winsock; the socket was put in non-blocking mode above, which is
    // what the flag is here for. sendto takes an int length and returns int.
    int res = sendto(static_cast<SOCKET>(m_socketFd), payload.c_str(), static_cast<int>(payload.length()), 0,
                     reinterpret_cast<struct sockaddr*>(m_destAddr), sizeof(struct sockaddr_in));
#else
    ssize_t res = sendto(static_cast<int>(m_socketFd), payload.c_str(), payload.length(), MSG_DONTWAIT,
                         reinterpret_cast<struct sockaddr*>(m_destAddr), sizeof(struct sockaddr_in));
#endif
    if (res < 0)
    {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 10)
        {
            lastLog = now;
#ifdef _WIN32
            sLog.outError("TortoiseBots: Observability sendto failed (payload len=%zu, error=%d)", payload.length(), WSAGetLastError());
#else
            sLog.outError("TortoiseBots: Observability sendto failed (payload len=%zu, errno=%d)", payload.length(), errno);
#endif
        }
    }
}

bool ObservabilityEmitter::AnomalyAllowed(uint32 guid, uint8 typeId, uint32 nowMs)
{
    uint64 key = (static_cast<uint64>(guid) << 8) | typeId;
    auto it = m_anomalyCooldowns.find(key);
    if (it != m_anomalyCooldowns.end() && (nowMs - it->second) < kAnomalyCooldownMs)
        return false;

    if (m_anomalyCooldowns.size() >= kMaxAnomalyCooldowns)
        m_anomalyCooldowns.clear();
    m_anomalyCooldowns[key] = nowMs;
    return true;
}

void ObservabilityEmitter::EmitAnomaly(std::string const& type,
                                        std::string const& severity,
                                        Player* bot,
                                        std::string const& details,
                                        std::string const& targetName,
                                        std::string const& strategy,
                                        std::string const& lastAction,
                                        uint32 killerLevel)
{
    if (!IsEnabled())
        return;

    // Rate-limiting exists to collapse a sustained condition (stuck, action
    // loop, unreachable target) into one row per window. A death is an event,
    // not a condition: a bot dying twice inside one window died twice, and the
    // pool casualty rate must not lose the second one. So BOT_DEATH bypasses
    // the (guid, type) cooldown entirely.
    bool const rateLimited = type != "BOT_DEATH";
    if (bot && rateLimited && !AnomalyAllowed(bot->GetGUIDLow(), AnomalyTypeIdFromName(type), WorldTimer::getMSTime()))
        return;

    if (type == "BOT_DEATH" && bot)
        NoteDeathKiller(bot->GetGUIDLow(), targetName, killerLevel);

    // Callers that pass no strategy (the death path) still report the bot's
    // active strategy list, so every row carries the same context.
    std::string strat = strategy;
    if (strat.empty() && bot)
        strat = FormatStrategies(GET_PLAYERBOT_AI(bot));

    std::ostringstream ss;
    ss << "{\"v\":" << kProtocolVersion
       << ",\"ts\":" << time(nullptr)
       << ",\"type\":\"" << EscapeJson(type) << "\""
       << ",\"severity\":\"" << EscapeJson(severity) << "\"";

    if (bot)
    {
        ss << ",\"bot\":\"" << EscapeJson(bot->GetName()) << "\""
           << ",\"guid\":" << bot->GetGUIDLow()
           << ",\"class\":\"" << EscapeJson(GetBotClassName(bot->GetClass())) << "\""
           << ",\"level\":" << static_cast<uint32>(bot->GetLevel())
           << ",\"map\":" << bot->GetMapId()
           << ",\"zone\":" << bot->GetZoneId()
           << ",\"pos\":{\"x\":" << std::fixed << std::setprecision(1) << bot->GetPositionX()
           << ",\"y\":" << bot->GetPositionY()
           << ",\"z\":" << bot->GetPositionZ() << "}";
    }

    if (!targetName.empty())
        ss << ",\"target\":\"" << EscapeJson(targetName) << "\"";
    if (!strat.empty())
        ss << ",\"strategy\":\"" << EscapeJson(strat) << "\"";
    if (!lastAction.empty())
        ss << ",\"last_action\":\"" << EscapeJson(lastAction) << "\"";
    if (!details.empty())
        ss << ",\"details\":\"" << EscapeJson(details) << "\"";

    ss << "}";

    SendDatagram(ss.str());
}

void ObservabilityEmitter::OnActionFailed(Player* bot,
                                          std::string const& actionName,
                                          std::string const& targetName,
                                          std::string const& strategy)
{
    if (!IsEnabled() || !bot)
        return;

    uint32 nowMs = WorldTimer::getMSTime();
    std::string key = std::to_string(bot->GetGUIDLow()) + "|" + actionName;

    ActionFailureRecord& rec = m_actionFailures[key];
    if (rec.firstFailTimeMs == 0 || (nowMs - rec.firstFailTimeMs) > 2000)
    {
        rec.firstFailTimeMs = nowMs;
        rec.count = 1;
        rec.reported = false;
    }
    else
    {
        ++rec.count;
    }
    rec.lastFailTimeMs = nowMs;

    // A bot stuck on the same failing action would otherwise raise a new
    // anomaly every 2 s window; report each bot/action pair at most every 5 min.
    if (rec.count >= 5 && !rec.reported &&
        (rec.lastReportMs == 0 || (nowMs - rec.lastReportMs) > 300000))
    {
        rec.reported = true;
        rec.lastReportMs = nowMs;
        std::string details = "Action '" + actionName + "' failed >= 5 times within 2 seconds";
        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        std::string activeStrat = !strategy.empty() ? strategy : FormatStrategies(ai);
        EmitAnomaly("ACTION_LOOP", "WARN", bot, details, targetName, activeStrat, actionName);
    }
}

void ObservabilityEmitter::EmitBotActivity(std::string const& event,
                                            std::string const& info1,
                                            std::string const& info2,
                                            Player* bot)
{
    if (!IsEnabled() || !bot || !IsActivityEvent(event))
        return;

    std::ostringstream ss;
    ss << "{\"event\":\"" << EscapeJson(event) << "\"";
    if (!info1.empty())
        ss << ",\"info1\":\"" << EscapeJson(info1) << "\"";
    if (!info2.empty())
        ss << ",\"info2\":\"" << EscapeJson(info2) << "\"";
    ss << ",\"bot\":\"" << EscapeJson(bot->GetName()) << "\""
       << ",\"guid\":" << bot->GetGUIDLow()
       << ",\"class\":\"" << EscapeJson(GetBotClassName(bot->GetClass())) << "\""
       << ",\"level\":" << static_cast<uint32>(bot->GetLevel())
       << ",\"map\":" << bot->GetMapId()
       << ",\"zone\":" << bot->GetZoneId();

    if (IsItemEvent(event))
    {
        uint32 itemId = ParseItemId(info2);
        ItemPrototype const* proto = itemId ? sObjectMgr.GetItemPrototype(itemId) : nullptr;
        if (proto)
        {
            ss << ",\"item_id\":" << itemId
               << ",\"quality\":" << proto->Quality
               << ",\"sell\":" << proto->SellPrice
               << ",\"buy\":" << proto->BuyPrice;
        }
    }

    // The daemon derives earned/spent from successive balances; a cheat-gold
    // borrow is always restored before the next event, so the delta stays real.
    ss << ",\"money\":" << bot->GetMoney() << "}";

    if (m_pendingEvents.size() >= kMaxPendingEvents)
        m_pendingEvents.clear();
    m_pendingEvents.push_back(ss.str());
}

void ObservabilityEmitter::FlushBotEvents(uint64 seq)
{
    if (m_pendingEvents.empty())
        return;

    size_t const total = m_pendingEvents.size();
    for (size_t start = 0; start < total; start += kEventBatchSize)
    {
        size_t const end = std::min(start + kEventBatchSize, total);

        std::ostringstream ss;
        ss << "{\"v\":" << kProtocolVersion
           << ",\"session\":" << m_sessionId
           << ",\"seq\":" << seq
           << ",\"ts\":" << time(nullptr)
           << ",\"type\":\"BOT_EVENTS\",\"events\":[";
        for (size_t i = start; i < end; ++i)
        {
            if (i > start) ss << ",";
            ss << m_pendingEvents[i];
        }
        ss << "]}";
        SendDatagram(ss.str());
    }
    m_pendingEvents.clear();
}

void ObservabilityEmitter::AddStateTime(size_t stateIndex, uint32 diff)
{
    if (stateIndex >= kStateCount || diff == 0)
        return;

    m_stateWindow[m_stateBucketIndex][stateIndex] += diff;
    m_stateBucketElapsedMs += diff;

    while (m_stateBucketElapsedMs >= kStateBucketMs)
    {
        m_stateBucketElapsedMs -= kStateBucketMs;
        m_stateBucketIndex = (m_stateBucketIndex + 1) % kStateBucketCount;
        for (size_t s = 0; s < kStateCount; ++s)
            m_stateWindow[m_stateBucketIndex][s] = 0;
    }
}

void ObservabilityEmitter::PruneState(uint32 nowMs)
{
    for (auto it = m_botTracking.begin(); it != m_botTracking.end();)
    {
        if ((nowMs - it->second.lastSeenMs) > kBotTrackingTtlMs)
            it = m_botTracking.erase(it);
        else
            ++it;
    }
    if (m_botTracking.size() > kMaxTrackedBots)
        m_botTracking.clear();

    for (auto it = m_actionFailures.begin(); it != m_actionFailures.end();)
    {
        if ((nowMs - it->second.lastFailTimeMs) > kActionFailureTtlMs)
            it = m_actionFailures.erase(it);
        else
            ++it;
    }
    if (m_actionFailures.size() > kMaxActionFailures)
        m_actionFailures.clear();

    for (auto it = m_anomalyCooldowns.begin(); it != m_anomalyCooldowns.end();)
    {
        if ((nowMs - it->second) > kAnomalyCooldownTtlMs)
            it = m_anomalyCooldowns.erase(it);
        else
            ++it;
    }
}

void ObservabilityEmitter::NoteDeathKiller(uint32 guid, std::string const& name, uint32 level)
{
    if (m_deathKillers.size() >= kMaxDeathKillers && m_deathKillers.find(guid) == m_deathKillers.end())
        return;
    DeathKillerInfo info;
    info.name = name.empty() ? "unknown" : name;
    info.level = level;
    info.time = time(nullptr);
    m_deathKillers[guid] = std::move(info);
}

void ObservabilityEmitter::Update(uint32 diff)
{
    if (!IsEnabled())
        return;
    m_tickWindow.Add(diff);

    if (!m_hostResolved)
        RetryHostResolution(diff);

    uint32 nowMs = WorldTimer::getMSTime();
    std::vector<Player*> activeBots = BotManager::Instance().GetAllBots();
    if (m_externalRosterProvider)
        m_externalRosterProvider(activeBots);

    for (Player* bot : activeBots)
    {
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        BotTrackState& track = m_botTracking[bot->GetGUIDLow()];

        // Last-action name without the non-const getName() call: read once
        // here, reused for the activity check and the snapshot below.
        char const* lastActionName = "";
        if (ai)
        {
            if (Action const* last = ai->GetLastExecutedAction(ai->GetState()))
                lastActionName = const_cast<Action*>(last)->getName().c_str();
        }
        // Fresh tracks start idle (unknown past), not busy: when the
        // first observation shows no activity, the 45 s clock starts
        // expired instead of granting a grace window. Active newcomers
        // keep the timestamp just stamped.
        bool fresh = track.lastActivityMs == 0 && track.lastActionName.empty();
        Activity act = NoteActivity(bot, track.hasWorkTarget, track, lastActionName);
        if (act.any)
            track.lastActivityMs = nowMs;
        if (act.progress)
            track.churnSinceMs = 0;
        else if (act.churn && track.churnSinceMs == 0)
            track.churnSinceMs = nowMs;
        if (fresh && track.lastActivityMs == 0)
            track.lastActivityMs = nowMs - kIdleAfterMs;
        // Spawn-camp recency: a bot fighting in place or banking XP is doing
        // real work even when standing still between pulls. Both signals are
        // member reads already paid for (combat state) or beside (XP/level)
        // this tick's classification.
        uint8 state = BaseMacroState(bot, ai);
        if (state == STATE_COMBAT)
            track.lastCombatMs = nowMs;
        uint32 curXp = bot->GetUInt32Value(PLAYER_XP);
        uint32 curLevel = bot->GetLevel();
        if (track.lastSeenMs != 0 && (curXp != track.lastXp || curLevel != track.lastLevel))
            track.lastXpMs = nowMs;
        track.lastXp = curXp;
        track.lastLevel = curLevel;

        // Idle means really doing nothing: a base IDLE bot that did anything
        // inside kIdleAfterMs is busy while it made real progress, and stalled
        // when its only activity was churn (a travel target or action-name
        // changes) for that whole window. Combat, moving, resting and dead are
        // instant states, never gated. A recent fight or XP gain (kill, quest,
        // ding) inside kRecentFightMs vetoes stalled: the bot is camping a
        // spawn between pulls, not standing with a destination and getting
        // nowhere.
        if (state == STATE_IDLE)
        {
            if (track.lastActivityMs != 0 && (nowMs - track.lastActivityMs) < kIdleAfterMs)
            {
                bool churnOnly = track.churnSinceMs != 0 &&
                    (nowMs - track.churnSinceMs) >= kIdleAfterMs;
                bool recentFight = (track.lastCombatMs != 0 && (nowMs - track.lastCombatMs) < kRecentFightMs) ||
                    (track.lastXpMs != 0 && (nowMs - track.lastXpMs) < kRecentFightMs);
                state = (churnOnly && !recentFight) ? STATE_STALLED : STATE_BUSY;
            }
            else
            {
                // Nothing at all for kIdleAfterMs: any churn run is over.
                track.churnSinceMs = 0;
            }
        }
        track.stateIndex = state;
        track.lastSeenMs = nowMs;
        AddStateTime(state, diff);

        // Anomaly 1: stuck while an active movement generator owns the bot. The
        // displacement is judged once a second against the position sampled a second
        // earlier. Per world tick a running bot moves only ~0.35 yd, so a per-tick
        // comparison against 0.5 yd counted every bot that kept walking for 8 s as
        // "stuck" - four fifths of all anomalies on a realm with 20 low-level bots.
        if (state != STATE_MOVING)
        {
            track.stationaryMovementMs = 0;
            track.stuckReported = false;
        }
        if (track.lastSampleMs == 0 || nowMs - track.lastSampleMs >= 1000)
        {
            uint32 const elapsed = track.lastSampleMs ? nowMs - track.lastSampleMs : 0;
            // A gap of several seconds between two samples is the world thread
            // stalling (terrain or grid load), not the bot: nothing moved because
            // nothing ticked. Such a sample is taken but not judged - in the first
            // measurement three bots in three different zones were reported stuck
            // for exactly the same 15.4 s, which was one stall.
            bool const usable = elapsed > 0 && elapsed <= 5000;
            float dx = bot->GetPositionX() - track.lastX;
            float dy = bot->GetPositionY() - track.lastY;
            float deltaDist = std::sqrt(dx * dx + dy * dy);
            if (state == STATE_MOVING && usable && deltaDist < 0.5f)
            {
                track.stationaryMovementMs += elapsed;
                if (track.stationaryMovementMs >= 8000 && !track.stuckReported)
                {
                    track.stuckReported = true;
                    std::ostringstream dss;
                    dss << "Coordinates stationary for " << (track.stationaryMovementMs / 1000.0f)
                        << "s while in active movement state";
                    // Counter-only: the daemon counts STUCK in Prometheus but
                    // keeps it out of the Incidents ring buffer. The 60 s STUCK
                    // issue episode is the surfaced signal.
                    EmitAnomaly("STUCK", "WARN", bot, dss.str(), "",
                                FormatStrategies(ai), "move");
                }
            }
            else if (state == STATE_MOVING && usable)
            {
                track.stationaryMovementMs = 0;
                track.stuckReported = false;
            }
            track.lastX = bot->GetPositionX();
            track.lastY = bot->GetPositionY();
            track.lastZ = bot->GetPositionZ();
            track.lastSampleMs = nowMs;
        }

        // Anomaly 2: combat target unreachable / out of line of sight.
        Unit* combatTarget = bot->GetSelectedUnit();
        if (state == STATE_COMBAT && combatTarget && sServerFacade.IsHostileTo(bot, combatTarget))
        {
            float dist = bot->GetDistance(combatTarget);
            bool inLos = true;
            bool unreachable = (dist > 45.0f);
            if (!unreachable && (track.unreachableDurationMs > 0 || track.lastSampleMs == nowMs))
            {
                inLos = bot->IsWithinLOSInMap(combatTarget, true);
                unreachable = !inLos;
            }

            if (unreachable)
            {
                track.unreachableDurationMs += diff;
                // Re-report every cooldown window while the target stays
                // unreachable, so the daemon has a liveness signal and can
                // expire the episode when the condition clears.
                bool due = !track.unreachableReported ||
                    (nowMs - track.lastUnreachableReportMs) >= kAnomalyCooldownMs;
                if (track.unreachableDurationMs >= 10000 && due)
                {
                    track.unreachableReported = true;
                    track.lastUnreachableReportMs = nowMs;
                    std::ostringstream dss;
                    dss << "Combat target '" << combatTarget->GetName() << "' unreachable / out of LoS for "
                        << (track.unreachableDurationMs / 1000.0f) << "s (dist=" << std::fixed << std::setprecision(1) << dist
                        << ", inLos=" << (inLos ? "true" : "false") << ")";
                    EmitAnomaly("UNREACHABLE_TARGET", "WARN", bot, dss.str(), combatTarget->GetName(),
                                FormatStrategies(ai), "combat reach");
                }
            }
            else
            {
                track.unreachableDurationMs = 0;
                track.unreachableReported = false;
            }
        }
        else
        {
            track.unreachableDurationMs = 0;
            track.unreachableReported = false;
        }
    }

    // Server-info cadence runs on every world tick (real time), not inside
    // the 2 s snapshot gate: the snapshot block below returns early 39/40
    // ticks, which previously diluted the 5-min interval to ~200 min.
    m_serverInfoTimerMs += diff;
    if (m_serverInfoTimerMs == diff || m_serverInfoTimerMs >= 300000)
    {
        m_serverInfoTimerMs = 0;
        EmitServerInfo();
    }

    m_snapshotTimerMs += diff;
    if (m_snapshotTimerMs < kSnapshotIntervalMs)
        return;
    m_snapshotTimerMs = 0;

    // Refresh per-bot work-target flags at snapshot cadence (one AI-value
    // lookup per bot per 2 s, not per tick), then run the snapshot.
    for (Player* bot : activeBots)
    {
        if (!bot || !bot->IsInWorld())
            continue;
        BotTrackState& track = m_botTracking[bot->GetGUIDLow()];
        track.hasWorkTarget = HasActiveWorkTarget(GET_PLAYERBOT_AI(bot));
    }

    PruneState(nowMs);
    EmitSnapshotCycle(activeBots, diff);

    for (size_t i = 0; i < kArmoryBotsPerSnapshot && i < activeBots.size(); ++i)
    {
        Player* bot = activeBots[m_armoryOffset++ % activeBots.size()];
        if (bot && bot->IsInWorld())
            WriteArmoryStats(bot);
    }
}

void ObservabilityEmitter::WriteArmoryStats(Player* bot)
{
    // Spell power: the lowest magic school is the shared base, the rest is a
    // per-school bonus on top (the dashboard renders base + bonus).
    int32 schoolDmg[MAX_SPELL_SCHOOL];
    int32 baseDmg = 0;
    float spellCrit = 0.0f;
    for (int school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
    {
        schoolDmg[school] = bot->SpellBaseDamageBonusDone(SpellSchoolMask(1 << school));
        baseDmg = school == SPELL_SCHOOL_HOLY ? schoolDmg[school] : std::min(baseDmg, schoolDmg[school]);
        // Talents raise single schools; show the best one.
        spellCrit = std::max(spellCrit, bot->GetSpellCritPercent(SpellSchools(school)));
    }

    // Core keeps rage x10; store display units like the telemetry snapshot.
    uint32 maxPower[MAX_POWERS];
    for (int i = 0; i < MAX_POWERS; ++i)
        maxPower[i] = bot->GetMaxPower(Powers(i));
    maxPower[POWER_RAGE] /= 10;

    bool hasRanged = bot->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE) > 0.0f;

    CharacterDatabase.PExecute(
        "REPLACE INTO tortoise_bots_armory_stats (guid, maxhealth, maxpower1, maxpower2, maxpower3, maxpower4, maxpower5, "
        "strength, agility, stamina, intellect, spirit, armor, resHoly, resFire, resNature, resFrost, resShadow, resArcane, "
        "spellDamage, spellDmgHoly, spellDmgFire, spellDmgNature, spellDmgFrost, spellDmgShadow, spellDmgArcane, healingPower, "
        "blockPct, dodgePct, parryPct, meleeCritPct, rangedCritPct, spellCritPct, attackPower, rangedAttackPower, "
        "meleeDmgMin, meleeDmgMax, rangedDmgMin, rangedDmgMax, meleeSpeed, rangedSpeed, meleeHit, rangedHit, spellHit, manaRegen) "
        "VALUES (%u, %u, %u, %u, %u, %u, %u, %f, %f, %f, %f, %f, %d, %d, %d, %d, %d, %d, %d, "
        "%d, %d, %d, %d, %d, %d, %d, %d, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %d)",
        bot->GetGUIDLow(), bot->GetMaxHealth(), maxPower[0], maxPower[1], maxPower[2], maxPower[3], maxPower[4],
        bot->GetStat(STAT_STRENGTH), bot->GetStat(STAT_AGILITY), bot->GetStat(STAT_STAMINA),
        bot->GetStat(STAT_INTELLECT), bot->GetStat(STAT_SPIRIT), bot->GetArmor(),
        bot->GetResistance(SPELL_SCHOOL_HOLY), bot->GetResistance(SPELL_SCHOOL_FIRE), bot->GetResistance(SPELL_SCHOOL_NATURE),
        bot->GetResistance(SPELL_SCHOOL_FROST), bot->GetResistance(SPELL_SCHOOL_SHADOW), bot->GetResistance(SPELL_SCHOOL_ARCANE),
        baseDmg, schoolDmg[SPELL_SCHOOL_HOLY] - baseDmg, schoolDmg[SPELL_SCHOOL_FIRE] - baseDmg,
        schoolDmg[SPELL_SCHOOL_NATURE] - baseDmg, schoolDmg[SPELL_SCHOOL_FROST] - baseDmg,
        schoolDmg[SPELL_SCHOOL_SHADOW] - baseDmg, schoolDmg[SPELL_SCHOOL_ARCANE] - baseDmg,
        bot->SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_HOLY),
        bot->GetFloatValue(PLAYER_BLOCK_PERCENTAGE), bot->GetFloatValue(PLAYER_DODGE_PERCENTAGE),
        bot->GetFloatValue(PLAYER_PARRY_PERCENTAGE), bot->GetFloatValue(PLAYER_CRIT_PERCENTAGE),
        bot->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE), spellCrit,
        bot->GetTotalAttackPowerValue(BASE_ATTACK), hasRanged ? bot->GetTotalAttackPowerValue(RANGED_ATTACK) : 0.0f,
        bot->GetFloatValue(UNIT_FIELD_MINDAMAGE), bot->GetFloatValue(UNIT_FIELD_MAXDAMAGE),
        bot->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE), bot->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE),
        bot->GetAttackTime(BASE_ATTACK) / 1000.0f, hasRanged ? bot->GetAttackTime(RANGED_ATTACK) / 1000.0f : 0.0f,
        bot->m_modMeleeHitChance, bot->m_modRangedHitChance, bot->m_modSpellHitChance,
        bot->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA));
}

void ObservabilityEmitter::EmitSnapshotCycle(std::vector<Player*> const& activeBots, uint32 diff)
{
    std::vector<BotTelemetrySnapshot> botSnapshots;
    botSnapshots.reserve(activeBots.size());

    for (Player* bot : activeBots)
    {
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        auto trackIt = m_botTracking.find(bot->GetGUIDLow());
        uint8 state = trackIt != m_botTracking.end() ? trackIt->second.stateIndex : BaseMacroState(bot, ai);

        BotTelemetrySnapshot snap;
        snap.name = bot->GetName();
        snap.guid = bot->GetGUIDLow();
        snap.className = GetBotClassName(bot->GetClass());
        snap.role = GetBotRole(bot, ai);
        snap.level = bot->GetLevel();
        snap.xp = bot->GetUInt32Value(PLAYER_XP);
        snap.nextXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        // A dead unit reports GetHealth() == 1 in the core, which made corpse
        // bars read "1 HP" on the dashboard. Report the corpse as 0 HP.
        snap.hp = bot->IsAlive() ? bot->GetHealth() : 0;
        snap.maxHp = bot->GetMaxHealth();
        snap.power = bot->GetPower(bot->GetPowerType());
        snap.maxPower = bot->GetMaxPower(bot->GetPowerType());
        // Core stores rage x10 (0-1000); every other resource is already in
        // display units. Normalize here so all consumers agree.
        if (bot->GetPowerType() == POWER_RAGE)
        {
            snap.power /= 10;
            snap.maxPower /= 10;
        }
        snap.powerType = GetPowerTypeName(bot->GetPowerType());
        snap.mapId = bot->GetMapId();
        snap.zoneId = bot->GetZoneId();
        snap.x = bot->GetPositionX();
        snap.y = bot->GetPositionY();
        snap.z = bot->GetPositionZ();
        snap.o = bot->GetOrientation();
        Unit* target = bot->GetSelectedUnit();
        snap.target = target ? target->GetName() : "";
        snap.targetLevel = 0;
        // Any unit target (creature or player, e.g. a follow master) carries
        // a level; only self-selection (the no-hostile-target case) is 0.
        if (target && target != bot)
            snap.targetLevel = static_cast<uint32>(target->GetLevel());
        snap.strategy = FormatStrategies(ai);
        if (!bot->IsAlive())
        {
            auto killerIt = m_deathKillers.find(bot->GetGUIDLow());
            if (killerIt != m_deathKillers.end())
            {
                snap.killer = killerIt->second.name;
                snap.killerLevel = killerIt->second.level;
            }
        }
        else
            m_deathKillers.erase(bot->GetGUIDLow());
        snap.state = MacroStateName(state);
        FillTravelInfo(ai, snap.travelPurpose, snap.travelTo, snap.travelStatus, snap.travelDist);
        BotManager::Instance().GetAiVisitInfo(bot->GetGUIDLow(), snap.aiVisits, snap.aiAgeMs);

        if (ai)
        {
            // getName() is a non-const accessor on the action; the pointer is
            // only read for its name here.
            if (Action const* last = ai->GetLastExecutedAction(ai->GetState()))
                snap.lastAction = const_cast<Action*>(last)->getName();
            snap.lastTrigger = ai->GetLastEvent().getSource();
        }
        botSnapshots.push_back(snap);
    }

    // Rolling window ratios: how bots spent the recent window, not all time.
    uint64 totals[kStateCount] = {0};
    for (size_t b = 0; b < kStateBucketCount; ++b)
        for (size_t s = 0; s < kStateCount; ++s)
            totals[s] += m_stateWindow[b][s];

    uint64 grandTotal = 0;
    for (size_t s = 0; s < kStateCount; ++s)
        grandTotal += totals[s];

    auto ratio = [&](size_t s) -> double
    {
        return grandTotal ? static_cast<double>(totals[s]) / static_cast<double>(grandTotal) : 0.0;
    };

    uint64 seq = ++m_snapshotSeq;
    // Network sessions only: headless bot sessions never enter the
    // account-keyed network map, so GetActiveSessionCount() already excludes
    // them. The old "sessions minus bots" math read 0 for any human count
    // below the bot count; count real sessions with a live networked player
    // instead.
    uint32 humanCount = 0;
    for (auto const& pair : sWorld.GetAllSessions())
    {
        WorldSession* sess = pair.second;
        if (sess && sess->HasNetworkTransport() && sess->GetPlayer() && sess->GetPlayer()->IsInWorld())
            ++humanCount;
    }
    uint32 botCount = static_cast<uint32>(botSnapshots.size());

    std::map<std::pair<std::string, std::string>, uint32> countMap;
    for (BotTelemetrySnapshot const& b : botSnapshots)
        countMap[{b.className, b.role}]++;

    PlayerLagWindow::Stats const tick = m_tickWindow.Compute();
    std::ostringstream ss;
    ss << "{\"v\":" << kProtocolVersion
       << ",\"session\":" << m_sessionId
       << ",\"seq\":" << seq
       << ",\"ts\":" << time(nullptr)
       << ",\"type\":\"HEARTBEAT\""
       << ",\"uptime\":" << sWorld.GetUptime()
       << ",\"diff\":" << diff
       << ",\"diff_avg\":" << tick.avgMs
       << ",\"diff_worst\":" << tick.worstMs
       << ",\"lag_p50\":" << tick.p50Ms
       << ",\"lag_p95\":" << tick.p95Ms
       << ",\"window_secs\":" << (kStateBucketCount * kStateBucketMs / 1000)
       << ",\"humans\":" << humanCount
       << ",\"bots\":" << botCount
       << ",\"states\":{"
       << "\"combat\":" << std::fixed << std::setprecision(3) << ratio(STATE_COMBAT) << ","
       << "\"moving\":" << ratio(STATE_MOVING) << ","
       << "\"busy\":" << ratio(STATE_BUSY) << ","
       << "\"stalled\":" << ratio(STATE_STALLED) << ","
       << "\"resting\":" << ratio(STATE_RESTING) << ","
       << "\"dead\":" << ratio(STATE_DEAD) << ","
       << "\"idle\":" << ratio(STATE_IDLE)
       << "},\"counts\":[";

    bool firstCount = true;
    for (auto const& kv : countMap)
    {
        if (!firstCount) ss << ",";
        firstCount = false;
        ss << "{\"class\":\"" << EscapeJson(kv.first.first) << "\",\"role\":\"" << EscapeJson(kv.first.second) << "\",\"count\":" << kv.second << "}";
    }
    ss << "]}";

    SendDatagram(ss.str());

    // Activity events collected since the previous cycle. Sent between the
    // heartbeat and the roster batches so the store has already folded them
    // in when the last batch completes the cycle and publishes the roster.
    FlushBotEvents(seq);

    // Chunked roster batches complete the cycle opened by the heartbeat.
    size_t totalBatches = (botSnapshots.size() + kBatchSize - 1) / kBatchSize;
    for (size_t bIdx = 0; bIdx < totalBatches; ++bIdx)
    {
        size_t start = bIdx * kBatchSize;
        size_t end = std::min(start + kBatchSize, botSnapshots.size());

        std::ostringstream bss;
        bss << "{\"v\":" << kProtocolVersion
            << ",\"session\":" << m_sessionId
            << ",\"seq\":" << seq
            << ",\"ts\":" << time(nullptr)
            << ",\"type\":\"BOT_BATCH\""
            << ",\"batch_index\":" << bIdx
            << ",\"total_batches\":" << totalBatches
            << ",\"bots\":[";

        for (size_t i = start; i < end; ++i)
        {
            BotTelemetrySnapshot const& b = botSnapshots[i];
            if (i > start) bss << ",";
            bss << "{\"name\":\"" << EscapeJson(b.name) << "\""
                << ",\"guid\":" << b.guid
                << ",\"class\":\"" << EscapeJson(b.className) << "\""
                << ",\"role\":\"" << EscapeJson(b.role) << "\""
                << ",\"level\":" << b.level
                << ",\"xp\":" << b.xp
                << ",\"next_xp\":" << b.nextXp
                << ",\"hp\":" << b.hp
                << ",\"max_hp\":" << b.maxHp
                << ",\"power\":" << b.power
                << ",\"max_power\":" << b.maxPower
                << ",\"power_type\":\"" << EscapeJson(b.powerType) << "\""
                << ",\"map\":" << b.mapId
                << ",\"zone\":" << b.zoneId
                << ",\"x\":" << std::fixed << std::setprecision(1) << b.x
                << ",\"y\":" << b.y
                << ",\"z\":" << b.z
                << ",\"o\":" << std::setprecision(2) << b.o
                << ",\"target\":\"" << EscapeJson(b.target) << "\""
                << ",\"target_level\":" << b.targetLevel
                << ",\"killer\":\"" << EscapeJson(b.killer) << "\""
                << ",\"killer_level\":" << b.killerLevel
                << ",\"strategy\":\"" << EscapeJson(b.strategy) << "\""
                << ",\"state\":\"" << EscapeJson(b.state) << "\""
                << ",\"last_action\":\"" << EscapeJson(b.lastAction) << "\""
                << ",\"last_trigger\":\"" << EscapeJson(b.lastTrigger) << "\""
                << ",\"travel_purpose\":\"" << EscapeJson(b.travelPurpose) << "\""
                << ",\"travel_to\":\"" << EscapeJson(b.travelTo) << "\""
                << ",\"travel_status\":\"" << b.travelStatus << "\""
                << ",\"travel_dist\":" << b.travelDist
                << ",\"ai_visits\":" << b.aiVisits
                << ",\"ai_age_ms\":" << b.aiAgeMs << "}";
        }
        bss << "]}";
        SendDatagram(bss.str());
    }
}
// Effective running settings for the dashboard Server panel. Reads live core
// rate getters and AiPlayerbot fields — never .env or conf files — so what
// the panel shows is what the server actually runs. Small (~1 KB) JSON,
// sent at startup then every 5 min; no per-tick cost, no secrets.
void ObservabilityEmitter::EmitServerInfo()
{
    auto flag = [](bool on) -> char const* { return on ? "1" : "0"; };
    std::ostringstream ss;
    ss << "{\"v\":" << kProtocolVersion
       << ",\"session\":" << m_sessionId
       << ",\"seq\":" << m_snapshotSeq
       << ",\"ts\":" << time(nullptr)
       << ",\"type\":\"SERVER_INFO\""
       << ",\"module_version\":\"" << EscapeJson(BuildVersion()) << "\""
       << ",\"core_revision\":\"" << EscapeJson(CoreRevision()) << "\""
       << ",\"core_date\":\"" << EscapeJson(CoreRevisionDate()) << "\""
       << ",\"uptime\":" << sWorld.GetUptime()
       << ",\"max_level\":" << sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)
       << ",\"rates\":{"
       << "\"xp_kill\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_XP_KILL)
       << ",\"xp_kill_elite\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_XP_KILL_ELITE)
       << ",\"xp_quest\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_XP_QUEST)
       << ",\"xp_explore\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_XP_EXPLORE)
       << ",\"drop_money\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY)
       << ",\"drop_poor\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_POOR)
       << ",\"drop_normal\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_NORMAL)
       << ",\"drop_uncommon\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON)
       << ",\"drop_rare\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_RARE)
       << ",\"drop_epic\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_EPIC)
       << ",\"drop_legendary\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_LEGENDARY)
       << ",\"honor\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_HONOR)
       << ",\"rep_gain\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_REPUTATION_GAIN)
       << ",\"rep_low_kill\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_KILL)
       << ",\"rep_low_quest\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_QUEST)
       << ",\"talent\":" << sWorld.getConfig(CONFIG_FLOAT_RATE_TALENT)
       << ",\"bot_xp_mult\":" << sPlayerbotAIConfig.playerbotsXPrate
       << "}"
       << ",\"bots\":{"
       << "\"min_random\":" << sPlayerbotAIConfig.minRandomBots
       << ",\"max_random\":" << sPlayerbotAIConfig.maxRandomBots
       << ",\"update_interval\":" << sPlayerbotAIConfig.randomBotUpdateInterval
       << ",\"max_level\":" << sPlayerbotAIConfig.randomBotMaxLevel
       << ",\"group_nearby\":\"" << flag(sPlayerbotAIConfig.randomBotGroupNearby) << "\""
       << ",\"raid_nearby\":\"" << flag(sPlayerbotAIConfig.randomBotRaidNearby) << "\""
       << ",\"invite_player\":\"" << flag(sPlayerbotAIConfig.randomBotInvitePlayer) << "\""
       << ",\"timed_logout\":\"" << flag(sPlayerbotAIConfig.randomBotTimedLogout) << "\""
       << ",\"disable_random_levels\":\"" << flag(sPlayerbotAIConfig.disableRandomLevels) << "\""
       << ",\"level_ladder\":\"" << flag(sPlayerbotAIConfig.levelLadder) << "\""
       << ",\"auto_do_quests\":\"" << flag(sPlayerbotAIConfig.autoDoQuests) << "\""
       << ",\"disable_activity\":\"" << flag(sPlayerbotAIConfig.disableActivityPriorities) << "\""
       << ",\"active_alone\":" << sPlayerbotAIConfig.botActiveAlone
       << ",\"force_active_near\":\"" << flag(sPlayerbotAIConfig.forceActiveWhenNearPlayer) << "\""
       << ",\"limit_combat\":\"" << flag(sPlayerbotAIConfig.limitCombatActivity) << "\""
       << ",\"pool_budget_us\":" << sPlayerbotAIConfig.poolTickBudgetUs
       << ",\"pool_budget_gate_ms\":" << sPlayerbotAIConfig.poolBudgetWhenTickOverMs
       << ",\"ah_buyer\":\"" << flag(sPlayerbotAIConfig.ahMarketBuyer) << "\""
       << ",\"lft\":\"" << flag(sPlayerbotAIConfig.randomBotLftEnabled) << "\""
       << ",\"bg\":\"" << flag(sPlayerbotAIConfig.randomBotBgEnabled) << "\""
       << ",\"avoid_towns\":\"" << flag(sPlayerbotAIConfig.avoidHostileTowns) << "\""
       << ",\"leave_zones\":\"" << flag(sPlayerbotAIConfig.leaveOutgrownZones) << "\""
       << ",\"bot_loot_uncommon\":" << sPlayerbotAIConfig.botLootRateUncommon
       << ",\"bot_loot_rare\":" << sPlayerbotAIConfig.botLootRateRare
       << ",\"ah_market\":\"" << flag(sPlayerbotAIConfig.ahMarketEnabled) << "\""
       << ",\"auto_learn_trainer_spells\":\"" << flag(sPlayerbotAIConfig.autoLearnTrainerSpells) << "\""
       << ",\"auto_learn_quest_spells\":\"" << flag(sPlayerbotAIConfig.autoLearnQuestSpells) << "\""
       << ",\"level_up_mounts\":\"" << flag(sPlayerbotAIConfig.levelUpMounts) << "\""
       << ",\"turtle_mount_at_level\":" << sPlayerbotAIConfig.turtleMountAtLevel
       << "}"
       << ",\"diagnostics\":{"
       << "\"perf_mon\":\"" << flag(sPlayerbotAIConfig.perfMonEnabled) << "\""
       << ",\"bot_events\":\"" << flag(sPlayerbotAIConfig.hasLog("bot_events.csv")) << "\""
       << ",\"unreachable\":\"" << flag(sPlayerbotAIConfig.hasLog("unreachable_targets.csv")) << "\""
       << ",\"deaths\":\"" << flag(sPlayerbotAIConfig.hasLog("deaths.csv")) << "\""
       << "}}";
    SendDatagram(ss.str());
}

} // namespace TortoiseBots
