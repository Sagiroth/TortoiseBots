#pragma once

#include "Common.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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
    std::string powerType; // "mana", "rage", "energy", "focus", "happiness"
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    std::string target;
    std::string strategy;
    std::string state;       // "combat", "moving", "resting", "dead", "idle"
    std::string lastAction;  // last action the AI executed (loop detection)
    std::string lastTrigger; // event source that drove the last action
};

// ObservabilityEmitter sends non-blocking loopback UDP telemetry to the
// standalone tortoise-observability daemon.
//
// State ownership: per-bot tracking, anomaly cooldowns, and the rolling state
// histogram are bounded and pruned on every snapshot, so nothing accumulates
// for bots that have logged out or for actions that stopped failing.
//
// Snapshot protocol: every emitted roster is a self-contained cycle identified
// by a monotonically increasing Seq. One HEARTBEAT opens the cycle and
// BOT_BATCH datagrams complete it; the receiver only publishes a roster when
// every batch of a cycle has arrived.
class ObservabilityEmitter
{
public:
    static ObservabilityEmitter& Instance();

    void Initialize();
    void Shutdown();
    bool IsEnabled() const;

    // Called once per world tick with the world update diff.
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

    // State maintenance (world thread only).
    void PruneState(uint32 nowMs);
    bool AnomalyAllowed(uint32 guid, uint8 typeId, uint32 nowMs);
    void AddStateTime(size_t stateIndex, uint32 diff);
    void EmitSnapshotCycle(std::vector<Player*> const& activeBots, uint32 diff);

    // Storage-only handle: this header stays free of <winsock2.h>/<sys/socket.h>, the same
    // way m_destAddr below stays a void* rather than a struct sockaddr_in*. std::uintptr_t
    // is wide enough to hold a Windows SOCKET (which is pointer-sized, not int-sized, and
    // truncating it silently accepts the wrong descriptor on rare unlucky values) and an
    // ordinary POSIX fd equally.
    using SocketHandle = std::uintptr_t;
    static constexpr SocketHandle kInvalidSocket = static_cast<SocketHandle>(-1);

    bool m_enabled;
    std::string m_host;
    uint32 m_port;
    SocketHandle m_socketFd;
    void* m_destAddr; // struct sockaddr_in*

    // Guards socket teardown against a concurrent sender; emission and state
    // mutation stay on the world thread.
    mutable std::mutex m_socketMutex;

    uint32 m_snapshotTimerMs;
    // Epoch identifying this server process. The daemon outlives server
    // restarts, so it needs this to distinguish a fresh seq sequence from
    // stale cycles of the previous process.
    uint64 m_sessionId;
    uint64 m_snapshotSeq;

    struct BotTrackState
    {
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;
        uint32 lastSeenMs = 0;
        uint8 stateIndex = 0;
        uint32 stationaryMovementMs = 0;
        bool stuckReported = false;

        uint64 unreachableTargetGuid = 0;
        uint32 unreachableDurationMs = 0;
        bool unreachableReported = false;
        uint32 lastUnreachableReportMs = 0;
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

    // key = guid << 8 | anomaly type id
    std::map<uint64, uint32> m_anomalyCooldowns;

    // Rolling macro-state histogram: kStateBuckets buckets of kStateBucketMs
    // each, one column per state. Ratios therefore describe the recent window
    // instead of an all-time average.
    static constexpr size_t kStateBucketCount = 90;
    static constexpr uint32 kStateBucketMs = 2000;
    static constexpr size_t kStateCount = 5; // combat, moving, resting, dead, idle
    uint64 m_stateWindow[kStateBucketCount][kStateCount];
    size_t m_stateBucketIndex;
    uint32 m_stateBucketElapsedMs;
};

#define sObservabilityEmitter (::TortoiseBots::ObservabilityEmitter::Instance())

} // namespace TortoiseBots
