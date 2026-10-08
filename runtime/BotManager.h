#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <ctime>
#include <vector>
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"
#include "PoolPassRotation.h"
#ifndef MANGOS_OBJECT_GUID_H
// Lens/build fallback — core header not on analyzer include path.
enum { HIGHGUID_PLAYER = 0 };
class ObjectGuid {
public:
    ObjectGuid() {}
    ObjectGuid(uint32_t, uint32_t) {}
    bool IsEmpty() const { return true; }
    bool IsPlayer() const { return true; }
    uint32_t GetCounter() const { return 0; }
    std::string GetString() const { return ""; }
    bool operator==(ObjectGuid const&) const { return true; }
    bool operator!=(ObjectGuid const&) const { return false; }
    void Clear() {}
};
#endif

class WorldSession;
class Player;
class WorldPacket;
class Unit;

namespace TortoiseBots {

bool NormalizeHeadlessGmPresentation(::Player* bot);

enum class BotLifecycle
{
    PendingAdd,
    InWorld,
    Removing,
};

struct BotRecord
{
    uint32_t accountId = 0; // character account used by the Headless login
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid characterGuid;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid masterGuid; // live owner/master for Follow
    // Durable owner account. Zero denotes an unowned/random runtime record and
    // retains the historical accountId fallback for diagnostics.
    uint32_t ownerAccountId = 0;
    uint32_t ticksInWorld = 0;
    bool enteredWorld = false;
    bool random = false;
    bool syncedInWorld = false;
    // pi-lens-ignore: no-bit-fields
    BotLifecycle lifecycle = BotLifecycle::PendingAdd;
};

struct OwnedCharacter
{
    uint32_t ownerAccountId = 0;
    uint32_t characterAccountId = 0;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid characterGuid;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid masterGuid;
    std::string name;
    uint8_t classId = 0;
    bool characterOnline = false; // informational only; Headless state is authoritative
    uint32_t mapId = 0;
    uint32_t zoneId = 0;
    uint32_t areaId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
};

class PlayerbotAIAdapter;
struct BotEntry
{
    BotRecord record;
    std::unique_ptr<PlayerbotAIAdapter> aiAdapter;
    // Server time (WorldTimer ms) of this bot's last AI update; 0 = never.
    uint32_t lastAiUpdateMs = 0;
    // Teleport acks sent by UpdateBots, and how many left the near flag set.
    uint32_t teleportAcks = 0;
    uint32_t teleportAcksIgnored = 0;
    BotEntry() = default;
    ~BotEntry();
    BotEntry(BotEntry&&) = default;
    BotEntry& operator=(BotEntry&&) = default;
};

class BotManager
{
public:
    static BotManager& Instance();

    void OnWorldUpdate(uint32_t diff);
    void OnPlayerLogin(Player* player);
    void OnPlayerBeforeLogout(Player* player);
    void OnPlayerLogout(Player* player);
    void ReleaseToClient(Player* player);
    void Shutdown();

    // Manual control for testing — bool success; core owns Headless session
// pi-lens-ignore: clang:unknown_typename
    bool AddBot(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid = ObjectGuid());
    bool AddRandomBot(uint32_t accountId, ObjectGuid guid);
// pi-lens-ignore: clang:unknown_typename
    bool AddBotWithMaster(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid);
// pi-lens-ignore: clang:unknown_typename
    bool RemoveBot(ObjectGuid guid, bool save = true);
    // Post-rez rescue for random bots stuck where their level cannot survive.
    // Returns true when the bot was relocated to a validated level-fitting
    // point (death count reset). Fail-closed: any validation miss, non-random
    // record, master/group/BG membership, or disabled config keeps position.
    bool RelocateHopelessBot(::Player* bot);
    // Safety net for masterless pool bots below level 10 (LowbieGraveyardPolicy.h):
    // after any revive/teleport, a bot standing in an area rated above its
    // level goes back to its birthplace via homebind teleport (no hearth
    // cooldown) instead of walking. The home area must itself fit, so a
    // mis-set homebind can never bounce the bot somewhere worse. Fail-closed
    // like RelocateHopelessBot; no death count needed.
    bool SendStrandedLowbieHome(::Player* bot);
    // Time-based rescue for random bots stranded alive where their level
    // cannot survive (guarded towns etc. never produce the deaths that drive
    // RelocateHopelessBot). Same eligibility, +5 rule and destinations;
    // relocates after a grace period of continuous stranding.
    bool RelocateStrandedBot(::Player* bot);

    // Durable manual ownership is separate from the transient Headless record.
    // GetOwnedCharacters includes every undeleted same-account character plus
    // any explicit cross-account ownership rows, so offline alts remain
    // discoverable before their first Headless login.
    bool RegisterOwnedCharacter(uint32_t ownerAccountId, uint32_t characterAccountId,
        ObjectGuid characterGuid, ObjectGuid masterGuid);
    bool GetOwnedCharacter(ObjectGuid characterGuid, OwnedCharacter& result);
    std::vector<OwnedCharacter> GetOwnedCharacters(uint32_t ownerAccountId);

// pi-lens-ignore: clang:unknown_typename
    BotRecord* FindBot(ObjectGuid guid);
    // pi-lens-ignore: clang:unknown_typename
    bool IsBot(ObjectGuid guid) const;
    // Random bots are still module-owned records; this distinction keeps
    // population identity inside BotManager.
    bool IsRandomBot(ObjectGuid guid) const;

    // A bot is controllable only after the Headless session is active and the
    // adapter has registered a usable PlayerbotAI for the live Player.
    bool IsControllableBot(Player* player) const;

    // Snapshot of in-world bots owned by a master. Callers never receive the
    // manager's records or session pointers, only live Player identities.
    std::vector<Player*> GetBotsForMaster(ObjectGuid masterGuid) const;
    // Snapshot of every live module-owned bot for legacy holder adapters and
    // diagnostics. Ownership remains entirely inside BotManager.
    std::vector<Player*> GetAllBots() const;
    uint32_t GetBotCount() const { return static_cast<uint32_t>(m_bots.size()); }

    // Native follow command and durable master ownership.
// pi-lens-ignore: clang:unknown_typename
    bool SetBotFollow(ObjectGuid botGuid, ObjectGuid masterGuid);
    // Durable ownership seam for module-owned group adoption/release. Normal
    // owners are Network players; a Headless owner is accepted only when it is
    // another module-owned fixture in the same runtime.
    // This updates the BotRecord and live PlayerbotAI pointer without
    // replacing the existing Headless session.
// pi-lens-ignore: clang:unknown_typename
    bool BindBotMaster(ObjectGuid botGuid, ObjectGuid masterGuid);
// pi-lens-ignore: clang:unknown_typename
    bool ClearBotMaster(ObjectGuid botGuid);

    // Deterministic regression check for AddBot -> immediate RemoveBot.
// pi-lens-ignore: clang:unknown_typename
    bool RunPendingAddRemoveTest(uint32_t accountId, ObjectGuid guid);

    // For the spike test: if enabled, automatically perform the 7 steps.
// pi-lens-ignore: clang:unknown_typename
    void SetAutoTestEnabled(bool enable, uint32_t accountId = 0, ObjectGuid guid = ObjectGuid());
    bool IsAutoTestEnabled() const { return m_autoTestEnabled; }

    // Strict runtime packet journey: Headless outgoing, Network-master
    // outgoing where applicable, and automatic mature invite acceptance.
    // Real Network incoming delivery remains a manual-client gate.
    void SetPacketBridgeTestEnabled(bool enable, uint32_t accountId = 0,
        ObjectGuid masterGuid = ObjectGuid(), ObjectGuid botGuid = ObjectGuid());

private:
    BotManager() = default;
    ~BotManager() = default;

    bool IsLiveHeadlessBot(BotEntry const& entry, Player* player) const;
    void FinishAutoTest(bool passed);
    void UpdateAutoTest(uint32_t diff);
    void UpdatePacketBridgeTest(uint32_t diff);
    void UpdateBots(uint32_t diff);
    void DetachOwnedBots(Player* master);
    void RebindOwnedBots(Player* master);


    std::unordered_map<uint32_t, BotEntry> m_bots; // key = guid counter
    // Reentrancy guard for AI-driven removal. PlayerbotAI::UpdateAIInternal can
    // request its own removal (stunned/idle logout path) while its Update is on
    // the stack inside UpdateBots. Stopping the Headless session synchronously
    // there deletes the PlayerbotAI (`this`) via the logout hooks and erases
    // the BotEntry mid-update (SIGSEGV on return into UpdateBots). While the
    // guard is set, RemoveBot only marks Removing and queues the request; the
    // queue drains after the update loop leaves every AI stack.
    bool m_inBotUpdate = false;
    struct PendingBotRemoval
    {
        ObjectGuid characterGuid;
        bool save = true;
    };
    std::vector<PendingBotRemoval> m_pendingBotRemovals;
    // Round-robin rotation for the random-pool pass in UpdateBots, with the
    // cursor that lets a budgeted pass resume where the previous tick stopped
    // (see PoolPassRotation.h).
    PoolPassRotation m_poolRotation;
    // Cursor for budgeted Pass 2 (combat bots) round-robin iteration.
    uint32_t m_combatCursor = 0;
    // BOTPERF window: UpdateBots pass cost accumulated over ~30 s of tick time.
    uint64_t m_perfPassUsSum = 0;
    uint64_t m_perfPassUsMax = 0;
    uint32_t m_perfPassCount = 0;
    uint32_t m_perfElapsedMs = 0;
    bool m_autoTestEnabled = false;
    uint32_t m_autoTestAccount = 0;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid m_autoTestGuid;
    uint32_t m_autoTestTicks = 0;
    enum class AutoState { Idle, LoggingIn, InWorld, Saving, LoggingOut, Relogging, CleaningUp, Done };
    AutoState m_autoState = AutoState::Idle;
    bool m_autoTestPassed = false;
    bool m_packetTestEnabled = false;
    uint32_t m_packetTestAccount = 0;
    ObjectGuid m_packetTestMasterGuid;
    ObjectGuid m_packetTestBotGuid;
    uint32_t m_packetTestTicks = 0;
    // Dead-bot sweep (module-side safety net, independent of the pool AI
    // rotation): releases stuck corpses and revives stalled ghosts that never
    // got an AI tick. Own cadence from OnWorldUpdate, so a starved pool budget
    // cannot strand the dead.
    void SweepDeadBots(uint32_t diff);
    uint32_t m_deadSweepElapsedMs = 0;
    // First-seen timestamps (time(nullptr)) for dead bots awaiting release /
    // revive. A bot seen alive (or gone) drops out; only a bot dead across the
    // whole grace window is touched. Key = guid counter.
    std::unordered_map<uint32_t, time_t> m_deadSince;
    // First-seen timestamps for bots the sweep found mid-teleport; a bot seen
    // out of teleport drops out. Feeds the "stuck teleport" diagnostic line.
    std::unordered_map<uint32_t, time_t> m_teleportSince;
    void SweepStrandedBots(uint32_t diff);
    uint32_t m_strandedSweepElapsedMs = 0;
    std::unordered_map<uint32_t, time_t> m_strandedSince; // key = guid counter
    uint8_t m_packetTestStage = 0;
};
} // namespace TortoiseBots
