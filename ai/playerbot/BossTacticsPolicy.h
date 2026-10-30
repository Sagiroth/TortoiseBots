#pragma once

// Per-boss tactics table and its pure decision rules (opt-in, see
// docs/play/boss-tactics.md).
//
// The rows live in the world table `ai_boss_tactic`, keyed by map + boss
// creature entry, and are read ONCE at startup into BossTacticTable (loader:
// ai/playerbot/BossTacticsMgr.cpp). Lookups never touch the database. A row
// is "active" while a live creature with its boss entry attacks the bot's
// side; the strategy "boss tactics" then applies it.
//
// Pattern: mod-playerbots' per-map raid strategies (trigger -> action per
// boss mechanic) reimplemented as data rows; provenance in its PR.
//
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_boss_tactics_policy.cpp. The table has
// the same lifecycle as runtime/NamedLocationIndex.h: one loading thread adds
// rows, calls Seal() once, and readers see nothing before the seal.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "policy/combat/RearFlankPolicy.h"

namespace ai
{
    // Row kinds. The text form is what the `kind` column holds.
    enum class BossTacticKind : std::uint8_t
    {
        None = 0,
        Interrupt = 1,  // interrupt `spell_id` while the caster casts it
        KillFirst = 2,  // DPS bots kill `target_entry` before anything else; lowest `priority` first
        Behind = 3,     // non-tank bots stay out of the boss's front within `param` yd (frontal cleave)
        AvoidCast = 4,  // non-tank bots leave `param` yd around the caster while it casts `spell_id`
    };

    inline BossTacticKind ParseBossTacticKind(const std::string& text)
    {
        if (text == "interrupt")
            return BossTacticKind::Interrupt;
        if (text == "kill_first")
            return BossTacticKind::KillFirst;
        if (text == "behind")
            return BossTacticKind::Behind;
        if (text == "avoid_cast")
            return BossTacticKind::AvoidCast;
        return BossTacticKind::None;
    }

    struct BossTacticRow
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;    // creature whose presence in the fight activates the row
        BossTacticKind kind = BossTacticKind::None;
        std::uint32_t targetEntry = 0;  // creature the row is about; 0 = the boss itself
        std::uint32_t spellId = 0;      // interrupt, avoid_cast: the spell
        std::uint8_t priority = 0;      // kill_first: lower is killed first
        float param = 0.0f;             // behind, avoid_cast: radius in yards
    };

    // The creature a row is about.
    inline std::uint32_t TacticSubjectEntry(const BossTacticRow& row)
    {
        return row.targetEntry ? row.targetEntry : row.bossEntry;
    }

    class BossTacticTable
    {
    public:
        // Loading only. False after Seal(), for an unusable row, or when the
        // (map, boss, kind, target, spell) key is already there: the first row
        // wins, as it would under the table's primary key.
        bool Add(const BossTacticRow& row)
        {
            if (sealed_.load(std::memory_order_acquire))
                return false;
            if (row.map == 0 || row.bossEntry == 0 || row.kind == BossTacticKind::None)
                return false;
            if (row.kind == BossTacticKind::Interrupt && row.spellId == 0)
                return false;
            if (row.kind == BossTacticKind::Behind && !(row.param > 0.0f))
                return false;
            if (row.kind == BossTacticKind::AvoidCast && (row.spellId == 0 || !(row.param > 0.0f)))
                return false;
            std::vector<BossTacticRow>& rows = rows_[Key(row.map, row.bossEntry)];
            for (const BossTacticRow& have : rows)
            {
                if (have.kind == row.kind && have.targetEntry == row.targetEntry && have.spellId == row.spellId)
                    return false;
            }
            rows.push_back(row);
            maps_[row.map] = true;
            if (row.kind == BossTacticKind::AvoidCast)
                avoidCastMaps_[row.map] = true;
            ++size_;
            return true;
        }

        void Seal() { sealed_.store(true, std::memory_order_release); }
        bool Sealed() const { return sealed_.load(std::memory_order_acquire); }
        std::size_t Size() const { return size_; }

        // Whether the map has any row. False before the seal.
        bool HasMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && maps_.find(map) != maps_.end();
        }

        // Whether the map has an avoid_cast row (the reaction engine's pass and the cast hold run only there). False
        // before the seal.
        bool HasAvoidCastMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && avoidCastMaps_.find(map) != avoidCastMaps_.end();
        }

        // The rows of one boss entry, or null. Null before the seal.
        const std::vector<BossTacticRow>* Rows(std::uint32_t map, std::uint32_t bossEntry) const
        {
            if (!sealed_.load(std::memory_order_acquire))
                return nullptr;
            auto it = rows_.find(Key(map, bossEntry));
            return it == rows_.end() ? nullptr : &it->second;
        }

    private:
        static std::uint64_t Key(std::uint32_t map, std::uint32_t bossEntry)
        {
            return (static_cast<std::uint64_t>(map) << 32) | bossEntry;
        }

        std::unordered_map<std::uint64_t, std::vector<BossTacticRow>> rows_;
        std::unordered_map<std::uint32_t, bool> maps_;
        std::unordered_map<std::uint32_t, bool> avoidCastMaps_;
        std::size_t size_ = 0;
        std::atomic<bool> sealed_{false};
    };

    // Activation gate. The tactics change how bots play, so they are not on for
    // everybody: either the server owner opted in (AiPlayerbot.DungeonTactics = 1)
    // or this bot sits in a group a remote commander has bound (the commander
    // runs). Always off on a map without rows, so nothing else pays for it.
    inline bool BossTacticsWanted(bool optIn, bool inCommandedGroup, bool mapHasRows)
    {
        return mapHasRows && (optIn || inCommandedGroup);
    }

    // Rows of the bosses present in a fight.
    using ActiveBossRows = std::vector<const BossTacticRow*>;

    // True when a creature of `casterEntry` casting `spellId` must be interrupted.
    inline bool IsInterruptListed(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t spellId)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::Interrupt && row->spellId == spellId &&
                TacticSubjectEntry(*row) == casterEntry)
                return true;
        }
        return false;
    }

    // The radius to keep from a creature of `casterEntry` while it casts `spellId`: the widest
    // avoid_cast row that lists the pair, or 0 when none does.
    inline float AvoidCastRadius(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t spellId)
    {
        float radius = 0.0f;
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::AvoidCast && row->spellId == spellId &&
                TacticSubjectEntry(*row) == casterEntry && row->param > radius)
                radius = row->param;
        }
        return radius;
    }

    // Whether a bot holds a caster, so stays in its blast: only a tank the caster is on (its current victim). It holds
    // the caster and whatever else is on it, and walking it out drags them along. A caster on nobody, or on a bot that
    // is no tank, keeps nobody in: an off-tank and a healer the caster turned to leave like anyone else.
    inline bool HoldsCaster(bool botIsTank, bool casterIsOnBot)
    {
        return botIsTank && casterIsOnBot;
    }

    // Whether a bot `distance` yd from such a caster has to leave (see HoldsCaster).
    inline bool MustLeaveCast(bool holdsCaster, float radius, float distance)
    {
        return !holdsCaster && radius > 0.0f && distance < radius;
    }

    // The escape points lie this far past the radius, so a caster shifting a little between
    // ticks does not put the bot back on the edge.
    constexpr float kAvoidCastStepOut = 2.0f;
    constexpr int kAvoidCastPoints = 7;

    struct AvoidCastPoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Escape points on a circle of radius + kAvoidCastStepOut around the caster: straight out
    // along the caster->bot bearing first (the shortest walk), then 45, 90 and 135 degrees to
    // either side, alternating. A bot standing on the caster has no bearing and uses
    // `fallbackAngle`. Returns the number of points written (kAvoidCastPoints).
    inline int AvoidCastDestinations(float casterX, float casterY, float botX, float botY, float radius,
                                     float fallbackAngle, AvoidCastPoint* out)
    {
        float const dx = botX - casterX;
        float const dy = botY - casterY;
        float const bearing = (dx * dx + dy * dy) < 0.01f ? fallbackAngle : std::atan2(dy, dx);
        float const ring = radius + kAvoidCastStepOut;
        float const step = 3.14159265f / 4.0f;
        float const offsets[kAvoidCastPoints] = { 0.0f, step, -step, 2 * step, -2 * step, 3 * step, -3 * step };
        for (int i = 0; i < kAvoidCastPoints; ++i)
        {
            out[i].x = casterX + ring * std::cos(bearing + offsets[i]);
            out[i].y = casterY + ring * std::sin(bearing + offsets[i]);
        }
        return kAvoidCastPoints;
    }

    // A listed cast in progress, as plain numbers: the caster's position and the radius to keep from it (3D, centre to
    // centre, as the core measures the blast).
    struct CastDangerZone
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
    };

    inline bool InsideCastZone(const CastDangerZone& zone, float x, float y, float z)
    {
        float const dx = zone.x - x;
        float const dy = zone.y - y;
        float const dz = zone.z - z;
        return zone.radius > 0.0f && std::sqrt(dx * dx + dy * dy + dz * dz) < zone.radius;
    }

    inline bool InsideAnyCastZone(const std::vector<CastDangerZone>& zones, float x, float y, float z)
    {
        for (const CastDangerZone& zone : zones)
            if (InsideCastZone(zone, x, y, z))
                return true;
        return false;
    }

    // The 2D distance from (px, py) to the segment (ax, ay)-(bx, by).
    inline float SegmentPointDistance2D(float ax, float ay, float bx, float by, float px, float py)
    {
        float const dx = bx - ax;
        float const dy = by - ay;
        float const len2 = dx * dx + dy * dy;
        float u = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
        u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        float const cx = ax + u * dx - px;
        float const cy = ay + u * dy - py;
        return std::sqrt(cx * cx + cy * cy);
    }

    // A walk-out's straight line may not take the bot deeper into a blast it stands in (closer to that caster than it
    // starts, kAvoidCastChordSlack aside) nor through a blast it is outside of. The bot starts inside its own blast, so
    // that one only has to grow away from the caster; a point 135 degrees off the bearing would cross the caster.
    constexpr float kAvoidCastChordSlack = 0.5f;

    inline bool AvoidCastChordClear(float botX, float botY, float destX, float destY,
                                    const std::vector<CastDangerZone>& zones)
    {
        for (const CastDangerZone& zone : zones)
        {
            float const sx = botX - zone.x;
            float const sy = botY - zone.y;
            float const start = std::sqrt(sx * sx + sy * sy);
            float const closest = SegmentPointDistance2D(botX, botY, destX, destY, zone.x, zone.y);
            if (start < zone.radius)
            {
                if (closest < start - kAvoidCastChordSlack)
                    return false;
            }
            else if (closest < zone.radius)
                return false;
        }
        return true;
    }

    // The escape points worth trying, shortest walk first: the ring points (AvoidCastDestinations) whose straight line
    // from the bot is clear (AvoidCastChordClear). Returns how many were written to `out` (at most kAvoidCastPoints).
    inline int AvoidCastEscapes(float casterX, float casterY, float botX, float botY, float radius, float fallbackAngle,
                                const std::vector<CastDangerZone>& zones, AvoidCastPoint* out)
    {
        AvoidCastPoint ring[kAvoidCastPoints];
        int const all = AvoidCastDestinations(casterX, casterY, botX, botY, radius, fallbackAngle, ring);
        int n = 0;
        for (int i = 0; i < all; ++i)
            if (AvoidCastChordClear(botX, botY, ring[i].x, ring[i].y, zones))
                out[n++] = ring[i];
        // Insertion sort by walk length; ties keep the ring's order (straight out, then the nearer turns).
        for (int i = 1; i < n; ++i)
        {
            AvoidCastPoint const p = out[i];
            float const d = (p.x - botX) * (p.x - botX) + (p.y - botY) * (p.y - botY);
            int j = i - 1;
            while (j >= 0 && (out[j].x - botX) * (out[j].x - botX) + (out[j].y - botY) * (out[j].y - botY) > d)
            {
                out[j + 1] = out[j];
                --j;
            }
            out[j + 1] = p;
        }
        return n;
    }

    // The cast hold: while listed casts run, a movement whose destination (its target's position, or a stay spot) lies
    // in one of them is held, so a bot does not walk back into a blast it left or into one beside it. Not the walk-out
    // itself, not a move whose target is the bot itself, and not the tank's move to the caster it holds (`zones` leaves
    // out the blasts of the casters the bot holds).
    inline bool CastHoldsMove(bool isWalkOut, bool targetIsSelf, bool hasDestination, float x, float y, float z,
                              const std::vector<CastDangerZone>& zones)
    {
        if (isWalkOut || targetIsSelf || !hasDestination)
            return false;
        return InsideAnyCastZone(zones, x, y, z);
    }

    // How a walk-out ended, for its bot_events.csv row: a walk started; MoveTo took a point but no walk started (a
    // collapsed path, a transport or a teleport wait); or no usable point.
    enum class AvoidCastWalk
    {
        None = 0,
        Started = 1,
        Deferred = 2,
    };

    inline const char* AvoidCastWalkName(AvoidCastWalk walk)
    {
        switch (walk)
        {
            case AvoidCastWalk::Started:
                return "started";
            case AvoidCastWalk::Deferred:
                return "deferred";
            case AvoidCastWalk::None:
                break;
        }
        return "none";
    }

    // The avoid_cast walk-outs a bot met lately, kept to log a cast once per bot. A cast is the same while the bot keeps
    // meeting it: same caster, same spell, same cast instance (the address of the caster's current Spell object, new
    // for every cast) and seen again within kAvoidCastLogGapMs; the gap covers an allocator handing a finished cast's
    // address to the next one. The bot keeps several, since the nearest of two casters at once can alternate.
    constexpr std::uint32_t kAvoidCastLogGapMs = 2000;
    constexpr int kAvoidCastLogCasts = 4;

    struct AvoidCastLogEntry
    {
        bool known = false;
        std::uint64_t casterGuid = 0;
        std::uint32_t spellId = 0;
        std::uint64_t instance = 0;
        std::uint32_t lastSeenMs = 0;
    };

    struct AvoidCastLogState
    {
        AvoidCastLogEntry casts[kAvoidCastLogCasts];
    };

    // True when this walk-out is the first of its cast for the bot; always records that it was seen now.
    inline bool AvoidCastIsNewCast(AvoidCastLogState& state, std::uint64_t casterGuid, std::uint32_t spellId,
                                   std::uint64_t instance, std::uint32_t nowMs)
    {
        AvoidCastLogEntry* slot = nullptr;
        for (AvoidCastLogEntry& e : state.casts)
        {
            if (!e.known || static_cast<std::uint32_t>(nowMs - e.lastSeenMs) >= kAvoidCastLogGapMs)
            {
                // Free, or a cast not seen for the gap: the slot can be reused (matching it counts as new).
                if (!slot || (slot->known && !e.known))
                    slot = &e;
                continue;
            }
            if (e.casterGuid == casterGuid && e.spellId == spellId && e.instance == instance)
            {
                e.lastSeenMs = nowMs;
                return false;
            }
        }
        if (!slot)
        {
            // All four are live: drop the one seen longest ago.
            slot = &state.casts[0];
            for (AvoidCastLogEntry& e : state.casts)
                if (static_cast<std::uint32_t>(nowMs - e.lastSeenMs) > static_cast<std::uint32_t>(nowMs - slot->lastSeenMs))
                    slot = &e;
        }
        slot->known = true;
        slot->casterGuid = casterGuid;
        slot->spellId = spellId;
        slot->instance = instance;
        slot->lastSeenMs = nowMs;
        return true;
    }

    // A live creature in the fight, as the kill-first pick sees it.
    struct TacticCandidate
    {
        std::uint32_t entry = 0;
        float distance = 0.0f;
        bool pickable = false;  // alive, attackable, reachable by the caller's rules
    };

    // Nobody is sent after a creature further away than this.
    constexpr float kBossTacticsEngageRange = 40.0f;

    // Index of the candidate to kill first, or -1 when no kill_first row names
    // a pickable candidate: lowest priority wins, the nearer one breaks a tie.
    inline int PickKillFirst(const ActiveBossRows& active, const std::vector<TacticCandidate>& candidates)
    {
        int best = -1;
        int bestPriority = 256;
        float bestDistance = 0.0f;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const TacticCandidate& c = candidates[i];
            if (!c.pickable || c.distance > kBossTacticsEngageRange)
                continue;
            int priority = 256;
            for (const BossTacticRow* row : active)
            {
                if (row->kind == BossTacticKind::KillFirst && TacticSubjectEntry(*row) == c.entry &&
                    static_cast<int>(row->priority) < priority)
                    priority = row->priority;
            }
            if (priority == 256)
                continue;
            if (best < 0 || priority < bestPriority || (priority == bestPriority && c.distance < bestDistance))
            {
                best = static_cast<int>(i);
                bestPriority = priority;
                bestDistance = c.distance;
            }
        }
        return best;
    }

    // Whether the bot stands in the boss's frontal half-plane within `radius`
    // yards (2D). Same front test as the generic rear flank (RearFlankPolicy.h).
    inline bool InsideBossFront(float bossX, float bossY, float bossFacing, float botX, float botY, float radius)
    {
        const float dx = botX - bossX;
        const float dy = botY - bossY;
        if (Dist2Sq(0.0f, 0.0f, dx, dy) > radius * radius)
            return false;
        return NeedsRearFlank(FlankHeadingDistance(bossFacing, std::atan2(dy, dx)), 0.0f);
    }

    // Flank destination for a bot in the boss's front: the nearer of the two
    // sides, 120 degrees off the facing (outside the front half-plane) at the
    // bot's own distance, never closer than `minDist`.
    inline void BossFlankDestination(float bossX, float bossY, float bossFacing, float botX, float botY,
                                     float minDist, float& outX, float& outY)
    {
        float dist = std::sqrt(Dist2Sq(bossX, bossY, botX, botY));
        if (dist < minDist)
            dist = minDist;
        float lx, ly, rx, ry;
        FlankOffset(bossX, bossY, bossFacing, kFlankMaxAngle, dist, lx, ly);
        FlankOffset(bossX, bossY, bossFacing, -kFlankMaxAngle, dist, rx, ry);
        if (Dist2Sq(lx, ly, botX, botY) <= Dist2Sq(rx, ry, botX, botY))
        {
            outX = lx;
            outY = ly;
        }
        else
        {
            outX = rx;
            outY = ry;
        }
    }
}
