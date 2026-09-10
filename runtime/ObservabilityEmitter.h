#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

class Player;
class Unit;

namespace TortoiseBots {

struct BotTelemetrySnapshot
{
    std::string name;
    uint32 guid = 0;
    std::string className;
    std::string role;
    uint32 level = 0;
    uint32 hp = 0;
    uint32 maxHp = 0;
    uint32 power = 0;
    uint32 maxPower = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    std::string target;
    std::string strategy;
    std::string state; // "combat", "moving", "resting", "dead", "idle"
};

class ObservabilityEmitter
{
public:
    static ObservabilityEmitter& Instance();

    void Initialize();
    void Shutdown();
    bool IsEnabled() const;

    void Update(uint32 diff);

    void EmitAnomaly(std::string const& type,
                     std::string const& severity,
                     Player* bot,
                     std::string const& details,
                     std::string const& targetName = "",
                     std::string const& strategy = "",
                     std::string const& lastAction = "");

    void OnActionFailed(Player* bot,
                        std::string const& actionName,
                        std::string const& targetName = "",
                        std::string const& strategy = "");

private:
    ObservabilityEmitter();
    ~ObservabilityEmitter();
    ObservabilityEmitter(ObservabilityEmitter const&) = delete;
    ObservabilityEmitter& operator=(ObservabilityEmitter const&) = delete;

    void SendDatagram(std::string const& payload);

    bool m_enabled;
    std::string m_host;
    uint32 m_port;
    int m_socketFd;
    void* m_destAddr; // struct sockaddr_in*
    std::mutex m_mutex;

    uint32 m_pulseTimerMs;

    struct BotTrackState
    {
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;
        uint32 stationaryMovementMs = 0;
        bool stuckReported = false;

        uint64 unreachableTargetGuid = 0;
        uint32 unreachableDurationMs = 0;
        bool unreachableReported = false;
    };
    std::map<uint32, BotTrackState> m_botTracking;

    struct ActionFailureRecord
    {
        uint32 count = 0;
        uint32 firstFailTimeMs = 0;
        uint32 lastFailTimeMs = 0;
        bool reported = false;
    };
    std::map<std::string, ActionFailureRecord> m_actionFailures;

    // Macro-state durations (ms) across active bots
    uint64 m_totalCombatMs = 0;
    uint64 m_totalMovingMs = 0;
    uint64 m_totalRestingMs = 0;
    uint64 m_totalDeadMs = 0;
    uint64 m_totalIdleMs = 0;
};

#define sObservabilityEmitter (::TortoiseBots::ObservabilityEmitter::Instance())

} // namespace TortoiseBots
