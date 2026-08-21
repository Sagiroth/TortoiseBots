#pragma once
#include <cstdint>
#include <string>
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"
#ifndef MANGOS_OBJECT_GUID_H
class ObjectGuid {
public:
    ObjectGuid() {}
    ObjectGuid(uint32_t, uint32_t) {}
    bool IsEmpty() const { return true; }
    uint32_t GetCounter() const { return 0; }
    std::string GetString() const { return ""; }
    bool operator==(ObjectGuid const&) const { return true; }
    bool operator!=(ObjectGuid const&) const { return false; }
    void Clear() {}
};
#endif

class Player;

namespace TortoiseBots {

enum class BotIntent
{
    None = 0,
    Follow = 1
};

class BotController
{
public:
    BotController(ObjectGuid botGuid, ObjectGuid masterGuid);
    ~BotController() = default;

    ObjectGuid GetBotGuid() const { return m_botGuid; }
    ObjectGuid GetMasterGuid() const { return m_masterGuid; }
    void SetMaster(ObjectGuid masterGuid);

    BotIntent GetIntent() const { return m_intent; }
    void SetIntent(BotIntent intent);

    // Called from BotManager::OnWorldUpdate on the world thread.
    // Resolves bot/master via sObjectAccessor each tick; no long-lived Player*.
    void Update(uint32_t diff);

    // Diagnostics
    const char* GetIntentName() const;

private:
    bool CanFollow(::Player* bot, ::Player* master) const;
    void DoFollow(::Player* bot, ::Player* master);

    ObjectGuid m_botGuid;
    ObjectGuid m_masterGuid;
    BotIntent m_intent = BotIntent::Follow;

    uint32_t m_nextUpdateMs = 0;

    // Follow restart guard (mirrors legacy FollowAction::isUseful)
    ObjectGuid m_lastFollowTarget;
    float m_lastFollowDist = -1.0f;
    float m_lastFollowAngle = -1000.0f;
    bool m_loggedFollowing = false;
};

} // namespace TortoiseBots
