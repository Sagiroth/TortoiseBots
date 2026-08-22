#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
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

namespace TortoiseBots {

class BotController;
enum class BotIntent;

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
    // pi-lens-ignore: no-bit-fields
    BotLifecycle lifecycle = BotLifecycle::PendingAdd;
};

class PlayerbotAIAdapter;
struct BotEntry
{
    BotRecord record;
    std::unique_ptr<BotController> controller; // legacy follow, kept for 1.5y dead-zone safety until Strategy/Trigger fully drives movement
    std::unique_ptr<PlayerbotAIAdapter> aiAdapter; // real PlayerbotAI/Engine/Strategy stack
};

class BotManager
{
public:
    static BotManager& Instance();

    void OnWorldUpdate(uint32_t diff);

    // Manual control for testing
// pi-lens-ignore: clang:unknown_typename
    WorldSession* AddBot(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid = ObjectGuid());
// pi-lens-ignore: clang:unknown_typename
    WorldSession* AddBotWithMaster(uint32_t accountId, ObjectGuid guid, ObjectGuid masterGuid);
// pi-lens-ignore: clang:unknown_typename
    bool RemoveBot(ObjectGuid guid, bool save = true);
// pi-lens-ignore: clang:unknown_typename
    BotRecord* FindBot(ObjectGuid guid);
// pi-lens-ignore: clang:unknown_typename
    bool IsBot(ObjectGuid guid) const;

    // Follow intent
// pi-lens-ignore: clang:unknown_typename
    bool SetBotFollow(ObjectGuid botGuid, ObjectGuid masterGuid);
// pi-lens-ignore: clang:unknown_typename
    BotController* GetController(ObjectGuid guid);
// pi-lens-ignore: clang:unknown_typename
    BotController const* GetController(ObjectGuid guid) const;

    // Deterministic regression check for AddBot -> immediate RemoveBot.
// pi-lens-ignore: clang:unknown_typename
    bool RunPendingAddRemoveTest(uint32_t accountId, ObjectGuid guid);

    // For the spike test: if enabled, automatically perform the 7 steps.
// pi-lens-ignore: clang:unknown_typename
    void SetAutoTestEnabled(bool enable, uint32_t accountId = 0, ObjectGuid guid = ObjectGuid());
    bool IsAutoTestEnabled() const { return m_autoTestEnabled; }

private:
    BotManager() = default;
    ~BotManager() = default;

    void UpdateAutoTest(uint32_t diff);
    void UpdateControllers(uint32_t diff);

    std::unordered_map<uint32_t, BotEntry> m_bots; // key = guid counter
    bool m_autoTestEnabled = false;
    uint32_t m_autoTestAccount = 0;
// pi-lens-ignore: clang:unknown_typename
    ObjectGuid m_autoTestGuid;
    uint32_t m_autoTestTicks = 0;
    enum class AutoState { Idle, LoggingIn, InWorld, Saving, LoggingOut, Relogging, Done };
    AutoState m_autoState = AutoState::Idle;
};

} // namespace TortoiseBots
