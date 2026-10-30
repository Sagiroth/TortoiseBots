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

    // Whether a bot `distance` yd from such a caster has to leave. The tank holding the caster stays
    // (it holds the caster and whatever else is on it, and walking it out drags them along); an
    // off-tank the caster is not on leaves like anyone else.
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

    // One bot's last avoid_cast walk-out, kept to log a cast once per bot. A cast is the same while the
    // bot keeps meeting it: same caster, same spell, same cast instance (the address of the caster's current Spell
    // object, new for every cast) and the walk-out seen again within kAvoidCastLogGapMs. The gap covers an
    // allocator handing a finished cast's address to the next one.
    constexpr std::uint32_t kAvoidCastLogGapMs = 2000;

    struct AvoidCastLogState
    {
        bool known = false;
        std::uint64_t casterGuid = 0;
        std::uint32_t spellId = 0;
        std::uint64_t instance = 0;
        std::uint32_t lastSeenMs = 0;
    };

    // True when this walk-out is the first of its cast for the bot; always records that it was seen now.
    inline bool AvoidCastIsNewCast(AvoidCastLogState& state, std::uint64_t casterGuid, std::uint32_t spellId,
                                   std::uint64_t instance, std::uint32_t nowMs)
    {
        bool const same = state.known && state.casterGuid == casterGuid && state.spellId == spellId &&
                          state.instance == instance &&
                          static_cast<std::uint32_t>(nowMs - state.lastSeenMs) < kAvoidCastLogGapMs;
        state.known = true;
        state.casterGuid = casterGuid;
        state.spellId = spellId;
        state.instance = instance;
        state.lastSeenMs = nowMs;
        return !same;
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
