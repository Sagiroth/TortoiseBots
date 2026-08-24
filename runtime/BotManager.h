#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"
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

namespace TortoiseBots {

enum class BotLifecycle
{
    PendingAdd,
    PendingLogin,
    InWorld,
    Removing,
};

struct BotRecord
{
    uint32_t accountId = 0;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid characterGuid;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid masterGuid; // owner/master for Follow
    uint32_t ticksInWorld = 0;
    bool enteredWorld = false;
    bool random = false;
    // pi-lens-ignore: no-bit-fields
    BotLifecycle lifecycle = BotLifecycle::PendingAdd;
};

class PlayerbotAIAdapter;
struct BotEntry
{
    BotRecord record;
    std::unique_ptr<PlayerbotAIAdapter> aiAdapter;
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

    // Manual control for testing
// pi-lens-ignore: clang:unknown_typename
    WorldSession* AddBot(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid = ObjectGuid());
    WorldSession* AddRandomBot(uint32_t accountId, ObjectGuid guid);
// pi-lens-ignore: clang:unknown_typename
    WorldSession* AddBotWithMaster(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid);
// pi-lens-ignore: clang:unknown_typename
    bool RemoveBot(ObjectGuid guid, bool save = true);
// pi-lens-ignore: clang:unknown_typename
    BotRecord* FindBot(ObjectGuid guid);
    // pi-lens-ignore: clang:unknown_typename
    bool IsBot(ObjectGuid guid) const;
    // Random bots are still module-owned records; this distinction prevents
    // behavior code from consulting a donor RandomPlayerbotMgr singleton.
    bool IsRandomBot(ObjectGuid guid) const;

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
    uint8_t m_packetTestStage = 0;
};

} // namespace TortoiseBots
