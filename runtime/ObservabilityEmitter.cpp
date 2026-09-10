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

} // anonymous namespace

ObservabilityEmitter& ObservabilityEmitter::Instance()
{
    static ObservabilityEmitter instance;
    return instance;
}

ObservabilityEmitter::ObservabilityEmitter()
    : m_enabled(false)
    , m_port(9195)
    , m_socketFd(-1)
    , m_destAddr(nullptr)
    , m_pulseTimerMs(0)
{
}

ObservabilityEmitter::~ObservabilityEmitter()
{
    Shutdown();
}

void ObservabilityEmitter::Initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_enabled = sPlayerbotAIConfig.observability ||
                sConfig.GetBoolDefault("AiPlayerbot.Observability", false) ||
                sConfig.GetBoolDefault("TortoiseBots.Observability", false);

    if (!m_enabled)
    {
        m_enabled = false;
        return;
    }

    m_port = sPlayerbotAIConfig.observabilityPort;
    if (m_port == 0)
        m_port = sConfig.GetIntDefault("AiPlayerbot.ObservabilityPort", 9195);
    if (m_port == 0)
        m_port = 9195;

    m_host = sConfig.GetStringDefault("AiPlayerbot.ObservabilityHost", "");
    if (m_host.empty())
    {
        if (char const* envHost = std::getenv("OBSERVABILITY_HOST"))
            m_host = envHost;
    }
    if (m_host.empty())
        m_host = "127.0.0.1";

    m_socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0)
    {
        sLog.outError("TortoiseBots: failed to create UDP socket for Observability emitter");
        m_enabled = false;
        return;
    }

    int flags = fcntl(m_socketFd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);

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
    m_destAddr = addr;

    sLog.outString("TortoiseBots: Observability telemetry active on %s:%u", m_host.c_str(), m_port);
}

void ObservabilityEmitter::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
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
    m_enabled = false;
    m_botTracking.clear();
    m_actionFailures.clear();
}

bool ObservabilityEmitter::IsEnabled() const
{
    return m_enabled && m_socketFd >= 0;
}

void ObservabilityEmitter::SendDatagram(std::string const& payload)
{
    if (!IsEnabled() || !m_destAddr)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socketFd < 0 || !m_destAddr)
        return;

    sendto(m_socketFd, payload.c_str(), payload.length(), MSG_DONTWAIT,
           reinterpret_cast<struct sockaddr*>(m_destAddr), sizeof(struct sockaddr_in));
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

    std::ostringstream ss;
    ss << "{\"ts\":" << time(nullptr)
       << ",\"type\":\"" << EscapeJson(type) << "\""
       << ",\"severity\":\"" << EscapeJson(severity) << "\"";

    if (bot)
    {
        ss << ",\"bot\":\"" << EscapeJson(bot->GetName()) << "\""
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

void ObservabilityEmitter::Update(uint32 diff)
{
    if (!IsEnabled())
        return;

    std::vector<Player*> activeBots = BotManager::Instance().GetAllBots();
    std::vector<BotTelemetrySnapshot> botSnapshots;
    botSnapshots.reserve(activeBots.size());

    for (Player* bot : activeBots)
    {
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        uint32 guidLow = bot->GetGUIDLow();
        BotTrackState& track = m_botTracking[guidLow];

        // Determine macro-state
        std::string stateStr = "idle";
        if (!sServerFacade.IsAlive(bot) || (ai && ai->GetState() == BotState::BOT_STATE_DEAD))
        {
            stateStr = "dead";
            m_totalDeadMs += diff;
        }
        else if (sServerFacade.IsInCombat(bot) || (ai && ai->GetState() == BotState::BOT_STATE_COMBAT))
        {
            stateStr = "combat";
            m_totalCombatMs += diff;
        }
        else if (bot->IsMoving() ||
                 (bot->GetMotionMaster() &&
                  (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ||
                   bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
                   bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)))
        {
            stateStr = "moving";
            m_totalMovingMs += diff;
        }
        else if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
        {
            stateStr = "resting";
            m_totalRestingMs += diff;
        }
        else
        {
            stateStr = "idle";
            m_totalIdleMs += diff;
        }

        // Anomaly 1: Stuck Detector
        bool isMovingState = (stateStr == "moving") ||
            (bot->GetMotionMaster() &&
             (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ||
              bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
              bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE));

        float dx = bot->GetPositionX() - track.lastX;
        float dy = bot->GetPositionY() - track.lastY;
        float deltaDist = std::sqrt(dx * dx + dy * dy);

        if (isMovingState && deltaDist < 0.5f)
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

        // Anomaly 2: Unreachable / Out of LoS Target
        Unit* combatTarget = bot->GetSelectedUnit();
        if (stateStr == "combat" && combatTarget && sServerFacade.IsHostileTo(bot, combatTarget))
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

        // Snapshot info
        BotTelemetrySnapshot snap;
        snap.name = bot->GetName();
        snap.guid = guidLow;
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
        snap.target = combatTarget ? combatTarget->GetName() : "";
        snap.strategy = FormatStrategies(ai);
        snap.state = stateStr;

        botSnapshots.push_back(snap);
    }

    // Periodic Heartbeat Pulse (every 2s)
    m_pulseTimerMs += diff;
    if (m_pulseTimerMs >= 2000)
    {
        m_pulseTimerMs = 0;

        uint64 totalStateMs = m_totalCombatMs + m_totalMovingMs + m_totalRestingMs + m_totalDeadMs + m_totalIdleMs;
        float rCombat = totalStateMs ? static_cast<float>(m_totalCombatMs) / totalStateMs : 0.0f;
        float rMoving = totalStateMs ? static_cast<float>(m_totalMovingMs) / totalStateMs : 0.0f;
        float rResting = totalStateMs ? static_cast<float>(m_totalRestingMs) / totalStateMs : 0.0f;
        float rDead = totalStateMs ? static_cast<float>(m_totalDeadMs) / totalStateMs : 0.0f;
        float rIdle = totalStateMs ? static_cast<float>(m_totalIdleMs) / totalStateMs : 0.0f;

        uint32 activeSessions = sWorld.GetActiveSessionCount();
        uint32 botCount = static_cast<uint32>(botSnapshots.size());
        uint32 humanCount = activeSessions > botCount ? (activeSessions - botCount) : 0;

        std::ostringstream ss;
        ss << "{\"ts\":" << time(nullptr)
           << ",\"type\":\"HEARTBEAT\""
           << ",\"uptime\":" << sWorld.GetUptime()
           << ",\"diff\":" << diff
           << ",\"humans\":" << humanCount
           << ",\"bots\":" << botCount
           << ",\"states\":{"
           << "\"combat\":" << std::fixed << std::setprecision(3) << rCombat << ","
           << "\"moving\":" << rMoving << ","
           << "\"resting\":" << rResting << ","
           << "\"dead\":" << rDead << ","
           << "\"idle\":" << rIdle
           << "}"
           << ",\"bot_list\":[";

        for (size_t i = 0; i < botSnapshots.size(); ++i)
        {
            BotTelemetrySnapshot const& b = botSnapshots[i];
            if (i > 0) ss << ",";
            ss << "{\"name\":\"" << EscapeJson(b.name) << "\""
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
               << ",\"state\":\"" << EscapeJson(b.state) << "\"}";
        }
        ss << "]}";

        SendDatagram(ss.str());
    }
}

} // namespace TortoiseBots
