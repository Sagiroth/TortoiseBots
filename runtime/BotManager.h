#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "Common.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <string>
#include <unordered_map>

class WorldSession;

// Minimal runtime for Phase 3 spike — not the full strategy/AI.
// Tracks headless sessions we created and drives the 7-step acceptance test
// when enabled via config or console.

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
    uint32 accountId = 0;
    ObjectGuid characterGuid;
    uint32 ticksInWorld = 0;
    bool enteredWorld = false;
    // pi-lens-ignore: no-bit-fields
    BotLifecycle lifecycle = BotLifecycle::PendingAdd;
};

class BotManager
{
public:
    static BotManager& Instance();

    void OnWorldUpdate(uint32 diff);

    // Manual control for testing
    WorldSession* AddBot(uint32 accountId, ObjectGuid guid);
    bool RemoveBot(ObjectGuid guid, bool save = true);
    BotRecord* FindBot(ObjectGuid guid);
    bool IsBot(ObjectGuid guid) const;

    // Deterministic regression check for AddBot -> immediate RemoveBot.
    bool RunPendingAddRemoveTest(uint32 accountId, ObjectGuid guid);

    // For the spike test: if enabled, automatically perform the 7 steps.
    void SetAutoTestEnabled(bool enable, uint32 accountId = 0, ObjectGuid guid = ObjectGuid());
    bool IsAutoTestEnabled() const { return m_autoTestEnabled; }

private:
    BotManager() = default;
    ~BotManager() = default;

    void UpdateAutoTest(uint32 diff);

    std::unordered_map<uint32, BotRecord> m_bots; // key = guid counter
    bool m_autoTestEnabled = false;
    uint32 m_autoTestAccount = 0;
    ObjectGuid m_autoTestGuid;
    uint32 m_autoTestTicks = 0;
    enum class AutoState { Idle, LoggingIn, InWorld, Saving, LoggingOut, Relogging, Done };
    AutoState m_autoState = AutoState::Idle;
};

} // namespace TortoiseBots
