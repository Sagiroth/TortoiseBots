#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

class Player;
class Unit;
class PlayerbotAI;

namespace ai
{
class WorldPosition;
}

namespace TortoiseBots
{

// Default-off bounded AH market population using native transaction path.
//
// Uses legitimate bot-owned inventory items, live AuctionHouse pricing APIs,
// and core-owned session/AuctionHouse handlers. No donor AhBot thread,
// no direct AuctionHouse map writes, no fake items, no DB scan per tick,
// no tick auction scan, no thread, no direct auction writes.
// Auctioneer proximity is solved via validated bounded teleport to an
// authoritative spawn location: one-time startup/first-enabled-tick snapshot
// from core creature data enumerates core creature data once (bounded
// one-shot cost) and is cached; subsequent ticks do not scan. Snapshot is
// filtered for event-unspawned, invalid/template-less, unknown-faction
// (fail-closed on missing faction) and terrain/VMap validated; marked loaded
// even when no valid positions exist so the service is dormant until
// restart/data reload rather than retrying every tick. Then native
// HandleAuctionSellItem. No invented coords. Per-bot attempt/failure cooldown
// via facade value store (before teleport/post, not only on success) prevents
// deposit/gold/no-auctioneer/invalid-spawn loops; successful-post cooldown
// (interval*2) and bounded batch (1..5 teleports/posts per tick) are preserved.
// Cross-feature safety (fail-closed): bots with an active PlayerbotAI player
// master, any grouped/manual-use bot (Player::GetGroup), LFT queued/in-offer
// (sLFTMgr.IsQueued/IsInOffer via core #416, read-only, no queue mutation),
// or inside a battleground/instance are never selected, posted, or teleported
// — market cannot pull a BG/dungeon/LFT/group/owned bot out of content.
// Per-bot AH action stays independent and never pulls owned/party bots from
// players. LFT guard hard-requires core PR #416 LFT queue seam (LFT/LFTMgr.h
// IsQueued/IsInOffer); build fails with #error if absent — no silent fallback.
class AhMarketService
{
public:
    static AhMarketService& Instance();

    void Update(uint32_t diff);

    // Rebuild / reload / command interfaces
    void ReloadOverrides();
    void RebuildMarket(bool all = false);
    std::string GetStatus() const;

    bool SetItemOverride(uint32_t itemId, uint32_t value, uint32_t addChance, uint32_t minAmount, uint32_t maxAmount);
    bool ResetItemOverride(uint32_t itemId);

    // Synthetic inventory & auction identification and isolation
    static constexpr uint32_t SYNTHETIC_OWNER_GUID = 0;
    static constexpr uint32_t SYNTHETIC_OWNER_ACCOUNT = 0;

    bool IsSyntheticAuction(uint32_t auctionId) const;
    bool IsSyntheticAuction(AuctionEntry const* auction) const;
    bool IsSyntheticItem(uint32_t itemGuidLow) const;
    bool IsItemBlacklisted(uint32_t itemId) const;

    // Isolation assertion: verifies synthetic item is NOT in player/bot inventory
    static bool AssertSyntheticIsolation(Player const* player, uint32_t itemGuidLow);

    enum class Phase : uint8_t
    {
        Idle,
        Gather,
        Overrides,
        Post,
        Buy,
        Expire
    };

    struct ItemOverride
    {
        uint32_t value = 0;       // Override buyout price per item (copper); if 0, item is blacklisted/banned
        uint32_t add_chance = 0;  // 0 = blacklisted/banned; >0 = posting chance %
        uint32_t min_amount = 0;
        uint32_t max_amount = 0;
    };

private:
    AhMarketService() = default;
    ~AhMarketService() = default;

    enum class PostResult { Posted, Teleported, Failed };

    struct AuctioneerPos
    {
        uint32_t mapId = 0;
        float x = 0, y = 0, z = 0, o = 0;
        uint32_t entry = 0;
    };

    struct GenerationSource
    {
        std::vector<uint32_t> ids;
        int32_t range[4] = {0, 0, 0, 0};
        LootStore* store = nullptr;
    };

    // Real-inventory seller path
    PostResult TryPostForBot(Player* bot, bool allowTeleport);
    Unit* FindNearbyAuctioneer(Player* bot, ::PlayerbotAI* ai);
    void EnsurePositionsLoaded();
    bool TryTeleportToAuctioneer(Player* bot);
    bool IsUsableAuctioneerPoint(ai::WorldPosition const& pos) const;
    bool IsBotInBattlegroundOrInstance(Player* bot) const;
    // Composed fail-closed guard for manual-use / LFT / BG content. World-thread read-only.
    bool IsBotAvailableForMarket(Player* bot) const;

    // Synthetic supply & buyer phased state machine with work budgets
    void StepWorkSlice();
    void StepPhase();
    void StepGather();
    void StepOverrides();
    void StepPost();
    void StepBuy();
    void StepExpire();
    void FinishPass();

    void EnsureSourcesLoaded();
    void RefreshDynamicLevel();
    bool IsItemEligible(ItemPrototype const* proto, bool forced = false) const;
    uint32_t CalculatePrice(ItemPrototype const* proto) const;
    uint32_t ApplyVariance(uint32_t price) const;
    uint32_t CalculateStack(ItemPrototype const* proto, uint32_t stockCount, uint32_t unitPrice) const;
    bool PublishSyntheticAuction(uint32_t itemId, uint32_t count, uint32_t unitPrice);
    bool BuyAuctionCandidate(AuctionEntry* auction, AuctionHouseObject* ahObject);

    // Teleport candidate buyer bot to auctioneer matching ahEntry
    Player* FindOrPrepareBuyerBot(AuctionHouseEntry const* ahEntry, bool allowTeleport);

    uint32_t m_elapsedMs = 0;
    size_t m_nextIndex = 0;
    std::vector<AuctioneerPos> m_auctioneerPositions;
    bool m_positionsLoaded = false;

    // Phased state machine members
    Phase m_phase = Phase::Idle;
    uint32_t m_currentHouse = 0;
    uint32_t m_actionCycle = 0;
    uint32_t m_rebuildRemaining = 0;
    uint32_t m_pendingRebuild = 0;
    time_t m_nextCycleCheck = 0;
    uint32_t m_maxRequiredLevel = 60;
    uint32_t m_maxItemLevel = 255;
    time_t m_nextLevelCheck = 0;

    std::vector<GenerationSource> m_sources;
    bool m_sourcesLoaded = false;
    std::vector<uint32_t> m_vendorItems;

    std::map<uint32_t, ItemOverride> m_overrides;
    bool m_overridesLoaded = false;
    uint32_t m_overrideCursor = 0;

    std::map<uint32_t, uint32_t> m_stock;
    size_t m_sourceIndex = 0;
    int32_t m_picksRemaining = -1;
    uint32_t m_rollsRemaining = 0;
    uint32_t m_selectedTemplate = 0;

    uint32_t m_scanHouse = 0;
    uint32_t m_scanCursor = 0;

    // Synthetic listing identification
    std::unordered_set<uint32_t> m_syntheticAuctions;
    std::unordered_set<uint32_t> m_syntheticItemGuids;

    // Work budget telemetry
    uint64_t m_lastSliceUs = 0;
    uint64_t m_maxSliceUs = 0;
    uint64_t m_totalListed = 0;
    uint64_t m_totalBought = 0;
    uint64_t m_totalExpired = 0;
    uint64_t m_totalProtectedBids = 0;
    uint64_t m_totalFailed = 0;
};

} // namespace TortoiseBots
