#pragma once

#include "Common.h"
#include "PlayerLagWindow.h"
#include <cstdint>
#include <ctime>
#include <map>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <future>

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
    uint32 xp = 0;        // PLAYER_XP: progress into the current level
    uint32 nextXp = 0;    // PLAYER_NEXT_LEVEL_XP: XP needed to finish the level
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
    uint32 targetLevel = 0;  // combat target level (0 = none/non-unit)
    std::string strategy;
    // Killer remembered when BOT_DEATH fires, shown on later snapshots while
    // the bot is dead. Never the live target: "target" stays the selected unit.
    std::string killer;
    uint32 killerLevel = 0;
    std::string state;       // "combat", "moving", "busy", "stalled", "resting", "dead", "idle"
    std::string lastAction;  // last action the AI executed (loop detection)
    std::string lastTrigger; // event source that drove the last action
    std::string travelPurpose; // active travel destination short name ("grind", "vendor", ...)
    std::string travelTo;      // active travel destination title (empty when idle)
    std::string travelStatus;  // "travel", "work", "cooldown", ... (empty when idle)
    int32 travelDist = -1;     // yards to the destination point (-1 when idle)
    uint32 aiVisits = 0;       // AI updates since login
    uint32 aiAgeMs = 0;        // ms since the last AI update
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

    struct BotTrackState
    {
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;
        uint32 lastSeenMs = 0;
        uint8 stateIndex = 0;
        uint32 stationaryMovementMs = 0;
        bool stuckReported = false;
        uint32 lastSampleMs = 0;      // when lastX/lastY were sampled (once a second)
        // Last world-tick time the bot did anything observable: moved,
        // executed an AI action, cast, looted, or held an active travel
        // target (flag refreshed at snapshot cadence, not per tick).
        // Idle requires none of these for >= kIdleAfterMs.
        uint32 lastActivityMs = 0;
        // Start of the current run of churn-only activity: a new last-action
        // name or an active travel target, with no movement, loot, cast or
        // sit. When the run reaches kIdleAfterMs the bot is stalled, not
        // busy; real progress (or a full kIdleAfterMs with no activity at
        // all) resets it to 0.
        uint32 churnSinceMs = 0;
        bool hasWorkTarget = false;
        // Last executed action name, to notice a new action without string
        // compares against history: any pointer/name change is activity.
        std::string lastActionName;

        uint64 unreachableTargetGuid = 0;
        uint32 unreachableDurationMs = 0;
        bool unreachableReported = false;
        uint32 lastUnreachableReportMs = 0;
    };

public:
    void EmitAnomaly(std::string const& type,
                     std::string const& severity,
                     Player* bot,
                     std::string const& details,
                     std::string const& targetName = "",
                     std::string const& strategy = "",
                     std::string const& lastAction = "",
                     uint32 killerLevel = 0);

    void OnActionFailed(Player* bot,
                        std::string const& actionName,
                        std::string const& targetName = "",
                        std::string const& strategy = "");

    // Activity stream: mirrors a whitelist of bot_events.csv rows (quests,
    // loot, vendor/trainer/repair, auctions, deaths, give-ups) into the
    // BOT_EVENTS datagram so the dashboard can roll them up per bot without
    // reading the CSV from another container. Item events are enriched with
    // the prototype's quality and prices; every event carries the bot's
    // copper so the daemon can derive earned/spent. Cheap: one small JSON
    // fragment per whitelisted row, flushed with the next snapshot cycle.
    void EmitBotActivity(std::string const& event,
                         std::string const& info1,
                         std::string const& info2,
                         Player* bot);

    // turtle: let another module (e.g. mod-turtlebots residents) contribute its
    // own Player* roster to every telemetry cycle. Called on the world thread.
    void SetExternalRosterProvider(std::function<void(std::vector<Player*>&)> provider);

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
    // Effective running settings for the dashboard Server panel (Addendum 2):
    // core rate getters + AiPlayerbot flags, sent at startup and on a slow
    // cadence. No strings from config files, no secrets — numbers and on/off.
    void EmitServerInfo();
    uint32 m_serverInfoTimerMs = 0;
    // Exact server-side stats for the dashboard armory
    // (tortoise_bots_armory_stats); a few bots per snapshot, round-robin.
    void WriteArmoryStats(Player* bot);
    size_t m_armoryOffset = 0;

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

    // Host resolution retry: when the configured host (e.g. the docker
    // service name) is not resolvable at startup, keep the configured name,
    // send to 127.0.0.1 meanwhile, and retry off the world thread every
    // kResolveRetryMs until it resolves (the dashboard may start later).
    static constexpr uint32 kResolveRetryMs = 30000;
    bool m_hostResolved = true;
    uint32 m_resolveRetryMs = 0;
    std::future<uint32_t> m_resolveFuture; // IPv4 s_addr (network order), 0 = failed
    void RetryHostResolution(uint32 diff);

    // Guards socket teardown against a concurrent sender; emission and state
    // mutation stay on the world thread.
    mutable std::mutex m_socketMutex;

    // Whitelisted activity events collected since the last snapshot, as
    // ready-to-join JSON fragments. Bounded: on overflow the batch is dropped
    // (the next cycle starts clean) the same way the other rolling tables
    // bound themselves.
    std::vector<std::string> m_pendingEvents;
    void FlushBotEvents(uint64 seq);

    uint32 m_snapshotTimerMs;
    // Epoch identifying this server process. The daemon outlives server
    // restarts, so it needs this to distinguish a fresh seq sequence from
    // stale cycles of the previous process.
    uint64 m_sessionId;
    uint64 m_snapshotSeq;
    // World-tick truth: every Update(diff) feeds the last 30 s of ticks, so
    // the heartbeat reports what players wait (see PlayerLagWindow.h)
    // instead of the one gated tick.
    PlayerLagWindow m_tickWindow;

    static constexpr size_t kMaxDeathKillers = 5000;

    struct DeathKillerInfo
    {
        std::string name;
        uint32 level = 0;
        time_t time = 0;
    };
    // guid -> killer from the last BOT_DEATH. Attached to later snapshots
    // while the bot is dead, cleared when the bot is seen alive again.
    std::unordered_map<uint32, DeathKillerInfo> m_deathKillers;
    void NoteDeathKiller(uint32 guid, std::string const& name, uint32 level);

    std::unordered_map<uint32, BotTrackState> m_botTracking;

    // Optional roster contributor from another module (world thread only).
    std::function<void(std::vector<Player*>&)> m_externalRosterProvider;

    struct ActionFailureRecord
    {
        uint32 count = 0;
        uint32 firstFailTimeMs = 0;
        uint32 lastFailTimeMs = 0;
        bool reported = false;
        uint32 lastReportMs = 0;
    };
    std::unordered_map<std::string, ActionFailureRecord> m_actionFailures;

    // key = guid << 8 | anomaly type id
    std::unordered_map<uint64, uint32> m_anomalyCooldowns;

    // Rolling macro-state histogram: kStateBuckets buckets of kStateBucketMs
    // each, one column per state. Ratios therefore describe the recent window
    // instead of an all-time average.
    // States: combat, moving, busy (real work while standing still —
    // looting, casting, eating, or movement within the last kIdleAfterMs),
    // stalled (standing still for >= kIdleAfterMs with nothing but churn: a
    // travel target or action-name changes), resting, dead, idle (no
    // movement, action, cast, loot, or active target for >= kIdleAfterMs).
    // Idle means really doing nothing, not "between actions".
    static constexpr size_t kStateBucketCount = 90;
    static constexpr uint32 kStateBucketMs = 2000;
    static constexpr size_t kStateCount = 7; // combat, moving, busy, stalled, resting, dead, idle
    static constexpr uint32 kIdleAfterMs = 45000;
    uint64 m_stateWindow[kStateBucketCount][kStateCount];
    size_t m_stateBucketIndex;
    uint32 m_stateBucketElapsedMs;
};

#define sObservabilityEmitter (::TortoiseBots::ObservabilityEmitter::Instance())

} // namespace TortoiseBots
