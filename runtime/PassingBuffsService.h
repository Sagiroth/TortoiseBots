#pragma once

#include <cstdint>
#include <ctime>
#include <unordered_map>
#include <utility>
#include <vector>

class Player;

namespace TortoiseBots
{

// Meeting-driven buffs from pool bots to real players (issue #1531): a
// passing priest casts Power Word: Fortitude, a druid Mark of the Wild,
// a mage Arcane Intellect (mana users), a paladin one Blessing fitting the
// player's class; a passing priest/druid lands Renew/Rejuvenation on a
// player below 35% health. Driven from the real players' side on a timer:
// every sweep looks up pool bots near each online real player with a grid
// search, never a per-bot scan of players. State is in memory only.
class PassingBuffsService
{
public:
    static PassingBuffsService& Instance();

    // BotManager::OnWorldUpdate.
    void Update(uint32_t diff);

private:
    PassingBuffsService() = default;

    struct PairKey
    {
        uint32_t botLow = 0;
        uint32_t playerLow = 0;
        bool operator==(PairKey const& other) const
        {
            return botLow == other.botLow && playerLow == other.playerLow;
        }
    };
    struct PairHash
    {
        std::size_t operator()(PairKey const& key) const noexcept
        {
            return (static_cast<std::size_t>(key.botLow) << 32) ^ key.playerLow;
        }
    };

    void SweepNow(std::vector<Player*> const& realPlayers);
    // One real player: find nearby pool bots, cast on new meetings.
    void VisitPlayer(Player* player);
    // Eligibility and cast for one (bot, player) pair. Returns true when a
    // cast started (cooldown recorded).
    bool TryBuff(Player* bot, Player* player, time_t now);
    bool TryHeal(Player* bot, Player* player, time_t now);

    // Per-(bot, player) last-cast times (unix seconds); pruned on write so
    // the maps stay bounded by the players met in one window.
    std::unordered_map<PairKey, time_t, PairHash> m_buffCooldowns;
    std::unordered_map<PairKey, time_t, PairHash> m_healCooldowns;
    // Pairs seen in range on the previous sweep, for new-meeting detection.
    std::unordered_map<PairKey, bool, PairHash> m_seenPairs;
    std::unordered_map<PairKey, bool, PairHash> m_seenPairsNext;

    uint32_t m_elapsedMs = 0;
};

} // namespace TortoiseBots
