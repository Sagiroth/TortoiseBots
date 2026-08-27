#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class ObjectGuid;

namespace TortoiseBots
{

// Default-off bounded fill of human-waiting LFT queues with live random Headless bots.
// Observes native queue via LFTManager::GetQueuedPlayers (copy, no private map exposure),
// identifies human groups/instances and missing 1 tank / 1 healer / 3 dps roles,
// filters in-memory Headless random candidates by level/team/hardcore/group/state/role
// (AiFactory / Script_GetAllowedRoles via BotLftRoleAdapter) and calls core
// LFTManager::QueuePlayer through native offers. When a native offer includes a
// service bot, calls generic LFTManager::AcceptOffer only for module-owned
// Headless bots (humans still explicitly accept). Rate-limited and reconciled
// when humans fill/leave. No second queue, no DB per tick, no addon hacks.
class LftBotFillService
{
public:
    static LftBotFillService& Instance();

    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    // True if this guidLow was queued by this service and is still pending
    // (queued or in offer). Used to constrain BotLftRoleAdapter to fill-owned bots.
    bool IsPending(uint32 guidLow) const;

private:
    LftBotFillService() = default;
    ~LftBotFillService() = default;

    bool IsEligibleCandidate(Player* bot) const;
    uint8 GetBotRoleMask(Player const* bot) const;
    bool IsModuleOwnedHeadlessBot(Player const* bot) const;
    void ReconcilePending(bool cancelAll, std::vector<std::string> const* activeInstances = nullptr);
    void AcceptPendingOffers();
    void ClearForcedRole(uint32 guidLow);

    bool m_initialized = false;
    uint32_t m_elapsedMs = 0;
    // guidLow -> instance we queued the bot for (single instance, the one we filled)
    std::unordered_map<uint32_t, std::string> m_pending;
};

} // namespace TortoiseBots
