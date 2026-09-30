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
#include "Log.h"
#include "../host/ModuleLog.h"
#include "../host/ModuleVersion.h"
#include "Timer.h"
#include "MotionMaster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace TortoiseBots {

namespace {

// Datagram schema version. The Go daemon ignores datagrams it cannot parse;
// this is bumped when the wire format changes incompatibly.
constexpr int kProtocolVersion = 6;

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
    if (type == "STUCK") return ANOMALY_STUCK;
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
// Active travel destination for the grinding panel. Cached AI values only
// (the value object already exists; Get() returns the pointer); GetShortName/
// GetTitle are cheap string builders on the already-chosen destination. Idle
// bots contribute empty strings so the pool rollup can count not-travelling.
void FillTravelInfo(PlayerbotAI* ai, std::string& purpose, std::string& to)
{
    purpose.clear();
    to.clear();
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

        uint8 state = DetermineMacroState(bot, ai);
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
            bool inLos = bot->IsWithinLOSInMap(combatTarget, true);
            float dist = bot->GetDistance(combatTarget);
            bool unreachable = (!inLos || dist > 45.0f);

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

    m_snapshotTimerMs += diff;
    if (m_snapshotTimerMs < kSnapshotIntervalMs)
        return;
    m_snapshotTimerMs = 0;

    PruneState(nowMs);
    EmitSnapshotCycle(activeBots, diff);

    // Server panel: first snapshot of the process, then every 5 min. Cheap
    // (a few dozen getters + one small datagram), no per-tick cost.
    if (m_serverInfoTimerMs == 0 || m_serverInfoTimerMs >= 300000)
    {
        m_serverInfoTimerMs = 1;
        EmitServerInfo();
    }
    else
        m_serverInfoTimerMs += diff;

    for (size_t i = 0; i < kArmoryBotsPerSnapshot && i < activeBots.size(); ++i)
    {
        Player* bot = activeBots[m_armoryCursor++ % activeBots.size()];
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
        uint8 state = trackIt != m_botTracking.end() ? trackIt->second.stateIndex : DetermineMacroState(bot, ai);

        BotTelemetrySnapshot snap;
        snap.name = bot->GetName();
        snap.guid = bot->GetGUIDLow();
        snap.className = GetBotClassName(bot->GetClass());
        snap.role = GetBotRole(bot, ai);
        snap.level = bot->GetLevel();
        snap.xp = bot->GetUInt32Value(PLAYER_XP);
        snap.nextXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        snap.hp = bot->GetHealth();
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
        if (target && target != bot && target->GetTypeId() == TYPEID_UNIT)
            snap.targetLevel = static_cast<uint32>(target->GetLevel());
        snap.strategy = FormatStrategies(ai);
        snap.state = MacroStateName(state);
        FillTravelInfo(ai, snap.travelPurpose, snap.travelTo);

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
                << ",\"strategy\":\"" << EscapeJson(b.strategy) << "\""
                << ",\"state\":\"" << EscapeJson(b.state) << "\""
                << ",\"last_action\":\"" << EscapeJson(b.lastAction) << "\""
                << ",\"last_trigger\":\"" << EscapeJson(b.lastTrigger) << "\""
                << ",\"travel_purpose\":\"" << EscapeJson(b.travelPurpose) << "\""
                << ",\"travel_to\":\"" << EscapeJson(b.travelTo) << "\"}";
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
       << ",\"core_revision\":\"" << EscapeJson(REVISION_HASH) << "\""
       << ",\"core_date\":\"" << EscapeJson(REVISION_DATE) << "\""
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
