#pragma once

// Per-bot ordered backlog of packets and chat a bot could not take yet
// (host/BotPacketAdapter): a bot between maps holds its items here, in arrival
// sequence, until it is back on its map or they go stale. Plain data and the
// standard library only, so tools/test_bot_packet_backlog.cpp exercises the
// order, the before-bound and the staleness standalone.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

namespace TortoiseBots
{

// One held item: its arrival sequence number, the bot it was queued for, the
// moment it was first held, and the caller's payload.
template <class Item>
struct HeldPacketEntry
{
    std::uint64_t seq = 0;
    std::uint64_t key = 0;
    std::uint64_t firstHeldMs = 0;
    Item item;
};

// World thread only: a bot guid maps to its held items. A key exists only
// while its backlog is non-empty.
template <class Item>
class BotPacketBacklog
{
public:
    void Append(std::uint64_t key, std::uint64_t seq, Item item, std::uint64_t nowMs)
    {
        m_backlogs[key].push_back(HeldPacketEntry<Item>{seq, key, nowMs, std::move(item)});
    }

    bool Contains(std::uint64_t key) const
    {
        return m_backlogs.find(key) != m_backlogs.end();
    }

    bool Empty() const
    {
        return m_backlogs.empty();
    }

    void Clear()
    {
        m_backlogs.clear();
    }

    // The bot's earliest held item arrived before `seq`. Items are kept in
    // arrival order, so the front decides: an item queued after the command
    // must not hold the command back (it ages out and would be lost).
    bool HasBefore(std::uint64_t key, std::uint64_t seq) const
    {
        auto const it = m_backlogs.find(key);
        return it != m_backlogs.end() && !it->second.empty() && it->second.front().seq < seq;
    }

    // As above over several bots (a master's owned bots).
    bool AnyBefore(std::vector<std::uint64_t> const& keys, std::uint64_t seq) const
    {
        for (std::uint64_t const key : keys)
            if (HasBefore(key, seq))
                return true;
        return false;
    }

    // Drops the bot's backlog when `gone(key)` (its AI left the storage), the
    // front entries older than `maxHeldMs`, then hands every entry queued
    // before `before` to `deliver(key, item)` for the bots `canDeliver(key)`
    // accepts, in arrival order.
    template <class Gone, class CanDeliver, class Deliver>
    void Drain(std::uint64_t before, std::uint64_t nowMs, std::uint64_t maxHeldMs, Gone gone, CanDeliver canDeliver,
        Deliver deliver)
    {
        for (auto it = m_backlogs.begin(); it != m_backlogs.end(); )
        {
            if (gone(it->first))
            {
                it = m_backlogs.erase(it);
                continue;
            }
            std::deque<HeldPacketEntry<Item>>& backlog = it->second;
            while (!backlog.empty() && nowMs - backlog.front().firstHeldMs > maxHeldMs)
                backlog.pop_front();
            if (canDeliver(it->first))
                while (!backlog.empty() && backlog.front().seq < before)
                {
                    deliver(backlog.front().key, backlog.front().item);
                    backlog.pop_front();
                }
            if (backlog.empty())
                it = m_backlogs.erase(it);
            else
                ++it;
        }
    }

private:
    std::map<std::uint64_t, std::deque<HeldPacketEntry<Item>>> m_backlogs;
};

// The command drain's deferral gate: a player's `.bot` command waits while a
// bot of that player still holds chat or packets queued before it. Draining
// first is what makes the check see them: the drain moves exactly those items
// into the backlog, so a `/p stay` typed before `/p .bot action follow` in the
// same tick is delivered first and follow lands last, as typed. Asking before
// the drain sees no backlog yet and lets the command overtake its own order.
// `drain(commandSeq)` is BotPacketAdapter::DrainDeferredPackets,
// `hasBefore(masterGuid, commandSeq)` is BotPacketAdapter::HasHeldPacketsBefore.
template <class Drain, class HasBefore>
inline bool DeferCommandForHeldPackets(std::uint64_t masterGuid, std::uint64_t commandSeq, Drain drain,
    HasBefore hasBefore)
{
    drain(commandSeq);
    return hasBefore(masterGuid, commandSeq);
}

} // namespace TortoiseBots
