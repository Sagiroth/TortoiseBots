#include "ObservabilityEmitter.h"

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/ServerFacade.h"
#include "Config/Config.h"
#include "Player.h"
#include "World.h"
#include "Log.h"
#include "Timer.h"
#include "MotionMaster.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace TortoiseBots {

namespace {

// Datagram schema version. The Go daemon ignores datagrams it cannot parse;
// this is bumped when the wire format changes incompatibly.
constexpr int kProtocolVersion = 3;

// Snapshot cadence and batching. Datagrams are kept well under the loopback
// MTU so a large roster arrives as several unpredictable chunks; the receiver
// assembles them per Seq.
constexpr uint32 kSnapshotIntervalMs = 2000;
constexpr size_t kBatchSize = 25;

// Retention bounds. All three tables are pruned on every snapshot, so a long
// uptime with heavy bot churn cannot grow them without limit.
constexpr uint32 kBotTrackingTtlMs = 60000;
constexpr uint32 kActionFailureTtlMs = 30000;
constexpr uint32 kAnomalyCooldownTtlMs = 600000;
constexpr size_t kMaxTrackedBots = 5000;
constexpr size_t kMaxActionFailures = 4096;
constexpr size_t kMaxAnomalyCooldowns = 8192;

// Per-bot/type anomaly cooldown, so a repeatedly failing bot cannot flood the
// dashboard and Prometheus counters.
constexpr uint32 kAnomalyCooldownMs = 30000;

enum MacroState : uint8
{
    STATE_COMBAT = 0,
    STATE_MOVING = 1,
    STATE_RESTING = 2,
    STATE_DEAD = 3,
    STATE_IDLE = 4,
};

char const* MacroStateName(uint8 state)
{
    switch (state)
    {
        case STATE_COMBAT:  return "combat";
        case STATE_MOVING:  return "moving";
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
    if (type == "BOT_STUCK") return ANOMALY_STUCK;
    if (type == "ACTION_LOOP") return ANOMALY_ACTION_LOOP;
    if (type == "UNREACHABLE_TARGET") return ANOMALY_UNREACHABLE;
    return ANOMALY_UNKNOWN;
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

uint8 DetermineMacroState(Player* bot, PlayerbotAI* ai)
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

    return STATE_IDLE;
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
    , m_socketFd(-1)
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

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        sLog.outError("TortoiseBots: failed to create UDP socket for Observability emitter");
        m_enabled = false;
        return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in* addr = new struct sockaddr_in();
    std::memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(static_cast<uint16>(m_port));

    if (inet_pton(AF_INET, m_host.c_str(), &addr->sin_addr) != 1)
    {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(m_host.c_str(), nullptr, &hints, &res) == 0 && res)
        {
            addr->sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        }
        else
        {
            sLog.outError("TortoiseBots: Observability failed to resolve host '%s', falling back to 127.0.0.1", m_host.c_str());
            m_host = "127.0.0.1";
            inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        m_socketFd = fd;
        m_destAddr = addr;
    }

    sLog.outString("TortoiseBots: Observability telemetry active on %s:%u", m_host.c_str(), m_port);
}

void ObservabilityEmitter::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_socketFd >= 0)
        {
            close(m_socketFd);
            m_socketFd = -1;
        }
        if (m_destAddr)
        {
            delete static_cast<struct sockaddr_in*>(m_destAddr);
            m_destAddr = nullptr;
        }
    }

    m_enabled = false;
    m_snapshotTimerMs = 0;
    m_snapshotSeq = 0;
    m_stateBucketIndex = 0;
    m_stateBucketElapsedMs = 0;
    std::memset(m_stateWindow, 0, sizeof(m_stateWindow));
    m_botTracking.clear();
    m_actionFailures.clear();
    m_anomalyCooldowns.clear();
}

bool ObservabilityEmitter::IsEnabled() const
{
    return m_enabled && m_socketFd >= 0;
}

void ObservabilityEmitter::SendDatagram(std::string const& payload)
{
    if (!IsEnabled())
        return;

    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_socketFd < 0 || !m_destAddr)
        return;

    ssize_t res = sendto(m_socketFd, payload.c_str(), payload.length(), MSG_DONTWAIT,
                         reinterpret_cast<struct sockaddr*>(m_destAddr), sizeof(struct sockaddr_in));
    if (res < 0)
    {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 10)
        {
            lastLog = now;
            sLog.outError("TortoiseBots: Observability sendto failed (payload len=%zu, errno=%d)", payload.length(), errno);
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
                                        std::string const& lastAction)
{
    if (!IsEnabled())
        return;

    if (bot && !AnomalyAllowed(bot->GetGUIDLow(), AnomalyTypeIdFromName(type), WorldTimer::getMSTime()))
        return;

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
    if (!strategy.empty())
        ss << ",\"strategy\":\"" << EscapeJson(strategy) << "\"";
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

    if (rec.count >= 5 && !rec.reported)
    {
        rec.reported = true;
        std::string details = "Action '" + actionName + "' failed >= 5 times within 2 seconds";
        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        std::string activeStrat = !strategy.empty() ? strategy : FormatStrategies(ai);
        EmitAnomaly("ACTION_LOOP", "WARN", bot, details, targetName, activeStrat, actionName);
    }
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

void ObservabilityEmitter::Update(uint32 diff)
{
    if (!IsEnabled())
        return;

    uint32 nowMs = WorldTimer::getMSTime();
    std::vector<Player*> activeBots = BotManager::Instance().GetAllBots();

    for (Player* bot : activeBots)
    {
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        BotTrackState& track = m_botTracking[bot->GetGUIDLow()];

        uint8 state = DetermineMacroState(bot, ai);
        track.stateIndex = state;
        track.lastSeenMs = nowMs;
        AddStateTime(state, diff);

        float dx = bot->GetPositionX() - track.lastX;
        float dy = bot->GetPositionY() - track.lastY;
        float deltaDist = std::sqrt(dx * dx + dy * dy);

        // Anomaly 1: stuck while an active movement generator owns the bot.
        if (state == STATE_MOVING && deltaDist < 0.5f)
        {
            track.stationaryMovementMs += diff;
            if (track.stationaryMovementMs >= 8000 && !track.stuckReported)
            {
                track.stuckReported = true;
                std::ostringstream dss;
                dss << "Coordinates stationary for " << (track.stationaryMovementMs / 1000.0f)
                    << "s while in active movement state";
                EmitAnomaly("BOT_STUCK", "WARN", bot, dss.str(), "",
                            FormatStrategies(ai), "move");
            }
        }
        else
        {
            track.stationaryMovementMs = 0;
            track.stuckReported = false;
        }

        track.lastX = bot->GetPositionX();
        track.lastY = bot->GetPositionY();
        track.lastZ = bot->GetPositionZ();

        // Anomaly 2: combat target unreachable / out of line of sight.
        Unit* combatTarget = bot->GetSelectedUnit();
        if (state == STATE_COMBAT && combatTarget && sServerFacade.IsHostileTo(bot, combatTarget))
        {
            bool inLos = bot->IsWithinLOSInMap(combatTarget, true);
            float dist = bot->GetDistance(combatTarget);
            bool unreachable = (!inLos || dist > 45.0f);

            if (unreachable)
            {
                track.unreachableDurationMs += diff;
                if (track.unreachableDurationMs >= 10000 && !track.unreachableReported)
                {
                    track.unreachableReported = true;
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

    m_snapshotTimerMs += diff;
    if (m_snapshotTimerMs < kSnapshotIntervalMs)
        return;
    m_snapshotTimerMs = 0;

    PruneState(nowMs);
    EmitSnapshotCycle(activeBots, diff);
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
        uint8 state = trackIt != m_botTracking.end() ? trackIt->second.stateIndex : DetermineMacroState(bot, ai);

        BotTelemetrySnapshot snap;
        snap.name = bot->GetName();
        snap.guid = bot->GetGUIDLow();
        snap.className = GetBotClassName(bot->GetClass());
        snap.role = GetBotRole(bot, ai);
        snap.level = bot->GetLevel();
        snap.hp = bot->GetHealth();
        snap.maxHp = bot->GetMaxHealth();
        snap.power = bot->GetPower(bot->GetPowerType());
        snap.maxPower = bot->GetMaxPower(bot->GetPowerType());
        snap.mapId = bot->GetMapId();
        snap.zoneId = bot->GetZoneId();
        snap.x = bot->GetPositionX();
        snap.y = bot->GetPositionY();
        snap.z = bot->GetPositionZ();
        snap.o = bot->GetOrientation();
        Unit* target = bot->GetSelectedUnit();
        snap.target = target ? target->GetName() : "";
        snap.strategy = FormatStrategies(ai);
        snap.state = MacroStateName(state);

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
    uint64 totals[kStateCount] = {0, 0, 0, 0, 0};
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

    uint32 activeSessions = sWorld.GetActiveSessionCount();
    uint32 botCount = static_cast<uint32>(botSnapshots.size());
    uint32 humanCount = activeSessions > botCount ? (activeSessions - botCount) : 0;

    std::map<std::pair<std::string, std::string>, uint32> countMap;
    for (BotTelemetrySnapshot const& b : botSnapshots)
        countMap[{b.className, b.role}]++;

    std::ostringstream ss;
    ss << "{\"v\":" << kProtocolVersion
       << ",\"session\":" << m_sessionId
       << ",\"seq\":" << seq
       << ",\"ts\":" << time(nullptr)
       << ",\"type\":\"HEARTBEAT\""
       << ",\"uptime\":" << sWorld.GetUptime()
       << ",\"diff\":" << diff
       << ",\"window_secs\":" << (kStateBucketCount * kStateBucketMs / 1000)
       << ",\"humans\":" << humanCount
       << ",\"bots\":" << botCount
       << ",\"states\":{"
       << "\"combat\":" << std::fixed << std::setprecision(3) << ratio(STATE_COMBAT) << ","
       << "\"moving\":" << ratio(STATE_MOVING) << ","
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
                << ",\"hp\":" << b.hp
                << ",\"max_hp\":" << b.maxHp
                << ",\"power\":" << b.power
                << ",\"max_power\":" << b.maxPower
                << ",\"map\":" << b.mapId
                << ",\"zone\":" << b.zoneId
                << ",\"x\":" << std::fixed << std::setprecision(1) << b.x
                << ",\"y\":" << b.y
                << ",\"z\":" << b.z
                << ",\"o\":" << std::setprecision(2) << b.o
                << ",\"target\":\"" << EscapeJson(b.target) << "\""
                << ",\"strategy\":\"" << EscapeJson(b.strategy) << "\""
                << ",\"state\":\"" << EscapeJson(b.state) << "\""
                << ",\"last_action\":\"" << EscapeJson(b.lastAction) << "\""
                << ",\"last_trigger\":\"" << EscapeJson(b.lastTrigger) << "\"}";
        }
        bss << "]}";
        SendDatagram(bss.str());
    }
}

} // namespace TortoiseBots
