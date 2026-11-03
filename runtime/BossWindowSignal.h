#pragma once

// Whether a boss is in a breath window, for the dashboard's boss overlay: a breath the core casts triggered never reaches
// a cast slot (core src/game/Spells/Spell.cpp:3640-3642), so the commander report cannot read it as the boss's current
// spell. This policy reads it from facts the report already holds instead: the boss's entry and map, its position, the
// window aura, whether it is stopped and whether it has a target, against the committed rows of
// runtime/BossWindowData.h. The report then sends the breath as the boss's cast (cast, cid, cleft, ctot and the lane's
// shape), the fields the dashboard already draws.
//
// Dashboard only: no bot behaviour reads it, and it reads no stand table.
//
// The countdown: a triggered breath has no cast timer to read, so the module times the window from its first sighting.
// A mark belongs to one unit in one instance (a creature's raw guid repeats across copies of a map), so the key is
// (map id, instance id, raw guid). The window closes when its time has run: a mark that has run its time stays spent
// until the state ends (Forget) or the unit goes unseen for longer than a window, which no single window can outlast,
// so a later sighting is a new window. Prune drops marks nobody has seen for kPruneMs.
//
// Standard library only, so tools/test_boss_window_signal.cpp compiles it without a game header.

#include "BossWindowData.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace TortoiseBots {
namespace BossWindow {

// What the report knows of a boss this cycle.
struct Facts
{
    uint32_t map = 0;
    uint32_t entry = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool stopped = false;
    bool hasTarget = false;
};

// The row whose window is open for the boss: of the boss's rows (map and entry), the hover point nearest to it within
// the row's radius (2D, on the radius counts), when the boss wears that row's window aura, is stopped and has no target.
// nullptr for none; a boss without rows never opens. `hasAura(id)` answers whether the boss wears an aura.
template <typename HasAura>
inline BreathPoint const* OpenPoint(BreathPoint const* rows, std::size_t count, Facts const& facts, HasAura hasAura)
{
    if (!facts.stopped || facts.hasTarget)
        return nullptr;
    BreathPoint const* best = nullptr;
    float bestDist2 = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        BreathPoint const& row = rows[i];
        if (row.map != facts.map || row.bossEntry != facts.entry)
            continue;
        float const dx = facts.x - row.x;
        float const dy = facts.y - row.y;
        float const dist2 = dx * dx + dy * dy;
        if (dist2 > row.radius * row.radius)
            continue;
        if (!best || dist2 < bestDist2)
        {
            best = &row;
            bestDist2 = dist2;
        }
    }
    if (best && best->windowAura != 0 && !hasAura(best->windowAura))
        return nullptr;
    return best;
}

// The same against the committed table.
template <typename HasAura>
inline BreathPoint const* OpenPoint(Facts const& facts, HasAura hasAura)
{
    return OpenPoint(kBreathPoints, std::size(kBreathPoints), facts, hasAura);
}

// How long a mark may go unseen before Prune drops it.
constexpr uint32_t kPruneMs = 60000;

struct Key
{
    uint32_t map = 0;
    uint32_t instance = 0;
    uint64_t guid = 0;

    bool operator==(Key const& other) const
    {
        return map == other.map && instance == other.instance && guid == other.guid;
    }
};

struct KeyHash
{
    std::size_t operator()(Key const& key) const
    {
        std::size_t h = static_cast<std::size_t>(key.map) * 0x9E3779B1u;
        h ^= static_cast<std::size_t>(key.instance) * 0x85EBCA77u;
        h ^= static_cast<std::size_t>(key.guid) * 0xC2B2AE3Du;
        h ^= static_cast<std::size_t>(key.guid >> 32) * 0x27D4EB2Fu;
        return h;
    }
};

// The open windows being timed. World thread only, like the rest of the commander snapshot.
class Countdowns
{
public:
    // A window of `totalMs` is open for this key now: true and the time left in `leftMs` while it runs, false once it
    // has run its time (the window is closed). The first sighting, or one after the key went unseen for longer than
    // `totalMs`, starts a new window.
    bool Left(Key const& key, uint32_t nowMs, uint32_t totalMs, uint32_t& leftMs)
    {
        auto it = marks_.find(key);
        if (it == marks_.end() || nowMs - it->second.seenMs > totalMs)
            it = marks_.insert_or_assign(key, Mark{nowMs, nowMs}).first;
        else
            it->second.seenMs = nowMs;
        uint32_t const elapsed = nowMs - it->second.startMs;
        if (elapsed >= totalMs)
            return false;
        leftMs = totalMs - elapsed;
        return true;
    }

    // The state ended (the boss casts from a slot, moves, or regains a target): the next window starts afresh.
    void Forget(Key const& key) { marks_.erase(key); }

    // Drops every mark not seen for kPruneMs.
    void Prune(uint32_t nowMs)
    {
        for (auto it = marks_.begin(); it != marks_.end();)
            it = nowMs - it->second.seenMs > kPruneMs ? marks_.erase(it) : std::next(it);
    }

    bool Contains(Key const& key) const { return marks_.find(key) != marks_.end(); }
    std::size_t Size() const { return marks_.size(); }

private:
    struct Mark
    {
        uint32_t startMs = 0; // the window's first sighting; the countdown runs from it
        uint32_t seenMs = 0;  // the last sighting
    };

    std::unordered_map<Key, Mark, KeyHash> marks_;
};

} // namespace BossWindow
} // namespace TortoiseBots
