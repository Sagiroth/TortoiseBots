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
// The service observes the copy-only native queue API from core #413, identifies
// human groups/instances and missing 1 tank / 1 healer / 3 dps roles, then filters
// in-memory Headless random candidates by the authoritative LFGDungeons.dbc range,
// team, hardcore, group, state, and AiFactory spec role before QueuePlayer.
// Native core owns offers, acceptance, cancellation, and group formation; the
// service auto-accepts only its own Headless participants. Unknown dungeon ranges
// fail closed. No second queue, DB tick, addon protocol, or role hook.
class LftBotFillService
{
public:
    static LftBotFillService& Instance();

    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

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
