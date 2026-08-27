// pi-lens-ignore: clang:all
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
// Cross-feature safety: bots queued or inside a battleground/instance are
// never selected, posted, or teleported, so the market cannot pull a
// BG/dungeon bot out of content once BG auto-queue is enabled.
class AhMarketService
{
public:
    static AhMarketService& Instance();

    void Update(uint32_t diff);

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

    PostResult TryPostForBot(Player* bot, bool allowTeleport);
    Unit* FindNearbyAuctioneer(Player* bot, ::PlayerbotAI* ai);
    void EnsurePositionsLoaded();
    bool TryTeleportToAuctioneer(Player* bot);
    bool IsUsableAuctioneerPoint(ai::WorldPosition const& pos) const;
    bool IsBotInBattlegroundOrInstance(Player* bot) const;

    uint32_t m_elapsedMs = 0;
    size_t m_nextIndex = 0;
    std::vector<AuctioneerPos> m_auctioneerPositions;
    bool m_positionsLoaded = false;
};

} // namespace TortoiseBots
