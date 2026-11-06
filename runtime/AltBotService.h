#pragma once

#include "AltBotPolicy.h"

#include "ObjectGuid.h"

#include <cstdint>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <vector>

class Group;
class Player;

namespace TortoiseBots
{

// Alt-bots (Issue #872). An owned alt the player brought in with `.bot add`
// becomes autonomous when it leaves the owner's party (kick or the party
// breaking up) and AiPlayerbot.AltBots is on: it drops its master and plays
// with the pool's leisure strategies until `.bot remove`, `.bot add` (taken
// back as a companion) or the player logging in on it.
//
// State is in memory only: alts do not come back after a restart. Lifecycle
// calls run on the world thread; the queries, the group queues and the
// summary counters may come from map threads, so the maps are locked.
class AltBotService
{
public:
    static AltBotService& Instance();

    bool IsAutonomous(uint32_t guidLow) const;
    bool IsAutonomous(ObjectGuid guid) const { return IsAutonomous(guid.GetCounter()); }
    uint32_t ActiveCount() const
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        return static_cast<uint32_t>(m_sessions.size());
    }
    uint32_t GetOwnerAccount(uint32_t guidLow) const;

    // Group hooks: a member left a group / a group broke up. Deferred to the
    // next Update so the group operation finishes before the bot is touched.
    void QueueGroupLeave(ObjectGuid guid);
    void QueueGroupDisband(Group* group);

    // BotManager::OnWorldUpdate.
    void Update();

    // `.bot add` on an autonomous alt: it becomes the requester's companion
    // again without a relog. Returns false when the alt is not autonomous.
    bool TakeBack(Player* requester, Player* bot);
    // BotManager::BindBotMaster: a master from the owner's account (`.bot add`,
    // an invite into the owner's party) ends the session.
    void OnMasterBound(Player* bot, Player* master);

    // Session end hooks. OnBotLogout: the Headless session is going away
    // (`.bot remove`, GM stop, idle logout, shutdown). OnReclaimed: the player
    // logged in on the alt with a real client.
    void OnBotLogout(Player* bot);
    void OnReclaimed(Player* player);
    // AltBotsLogoutWithOwner: the owner's alts log out with them.
    void OnOwnerLogout(Player* owner);
    // Window for the login summary: the owner's logout snapshots each alt;
    // an alt whose session starts later snapshots at session start. On the
    // owner's next login each autonomous alt answers one line over that
    // window, then the baseline stays until the next owner logout.
    void OnOwnerLogin(Player* owner);
    // One away-summary line per autonomous alt of this account, over the
    // window above, for `.bot alts` and the owner-login message.
    std::vector<std::pair<std::string, std::string>> AwayLines(uint32_t ownerAccountId);

    // In-memory summary counters.
    void OnQuestCompleted(Player* bot);
    void OnDeath(Player* bot);
    // Footprint: quests the alt took itself while autonomous. Only these may
    // be abandoned (DropQuestAction); cleared when the session ends.
    void AddTakenQuest(Player* bot, uint32_t questId);
    void AddTakenQuest(uint32_t guidLow, uint32_t questId);
    bool IsTakenQuest(uint32_t guidLow, uint32_t questId) const;

private:
    struct Session
    {
        uint32_t ownerAccountId = 0;
        ObjectGuid ownerGuid;
        AltBotSessionCounters counters;
        AltBotAwayPoint awayBaseline;
        bool awayBaselineSet = false;
        uint32_t displacedPoolGuidLow = 0;
        uint32_t displacedPoolAccountId = 0;
        std::vector<uint32_t> takenQuests;
    };

    AltBotService() = default;

    void TryStart(ObjectGuid guid);
    void DisplacePoolBot(Session& session, Player* alt);
    void End(uint32_t guidLow, Player* bot, AltBotEnd why);
    void RestoreDisplacedPoolBot(Session const& session);

    uint32_t m_instanceSweepMs = 0; // last alone-in-instance sweep (WorldTimer)
    mutable std::mutex m_sessionsMutex;
    std::unordered_map<uint32_t, Session> m_sessions; // key = alt guid counter
    std::mutex m_pendingMutex;
    std::vector<ObjectGuid> m_pendingLeaves;
};

} // namespace TortoiseBots
