#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class AuctionEntry;
class AuctionHouseObject;
class Player;
class Unit;
class PlayerbotAI;
struct ItemPrototype;

namespace ai
{
class WorldPosition;
}

namespace TortoiseBots
{

// Bounded organic AH market population using the native transaction path.
//
// Real-inventory seller loop: online random Headless bots with AH-profitable
// items in their bags post via HandleAuctionSellItem (with validated same-map
// auctioneer teleport fallback when already heading to an AH or nearby).
//
// Organic buyer (NO buyer teleport): pool bots already standing at an
// auctioneer of the auction's house bid on or buy listings using their own
// gold, bounded by per-tick microsecond and operation budgets.
class AhMarketService
{
public:
    static AhMarketService& Instance();

    void Update(uint32_t diff);

    // Clears per-bot attempt cooldown on activity-lease eviction.
    void OnLeaseEvicted(uint32_t guidLow);

    std::string GetStatus() const;

private:
    AhMarketService() = default;
    ~AhMarketService() = default;

    enum class PostResult { Posted, Teleported, Failed };

    enum class Phase : uint8_t
    {
        Idle,
        Buy
    };

    struct AuctioneerPos
    {
        uint32_t mapId = 0;
        float x = 0, y = 0, z = 0, o = 0;
        uint32_t entry = 0;
    };

    // Real-inventory seller path
    PostResult TryPostForBot(Player* bot, bool allowTeleport);
    Unit* FindNearbyAuctioneer(Player* bot, ::PlayerbotAI* ai);
    void EnsurePositionsLoaded();
    bool TryTeleportToAuctioneer(Player* bot);
    bool IsUsableAuctioneerPoint(ai::WorldPosition const& pos) const;
    bool IsBotInBattlegroundOrInstance(Player* bot) const;
    bool IsBotAvailableForMarket(Player* bot) const;
    static uint32_t SellerIntervalMs();

    // Organic buyer scan with per-tick work budgets
    void StepWorkSlice();
    void StepPhase();
    void StepBuy();
    void FinishPass();
    uint32_t CalculateBuyerBaselinePrice(ItemPrototype const* proto) const;
    uint32_t ApplyBuyerVariance(uint32_t price) const;
    bool BuyAuctionCandidate(AuctionEntry* auction, AuctionHouseObject* ahObject);

    uint32_t m_elapsedMs = 0;
    size_t m_nextIndex = 0;
    size_t m_buyerScanIndex = 0;
    std::vector<AuctioneerPos> m_auctioneerPositions;
    bool m_positionsLoaded = false;

    Phase m_phase = Phase::Idle;
    uint32_t m_actionCycle = 0;
    uint32_t m_scanHouse = 0;
    uint32_t m_scanCursor = 0;
    time_t m_nextCycleCheck = 0;

    uint64_t m_lastSliceUs = 0;
    uint64_t m_maxSliceUs = 0;
    uint64_t m_totalBought = 0;
};

} // namespace TortoiseBots
