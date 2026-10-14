#pragma once

// Per-boss tactics table and its pure decision rules (opt-in, see
// docs/guides/boss-tactics.md).
//
// The rows live in the world table `ai_boss_tactic`, keyed by map + boss
// creature entry, and are read ONCE at startup into BossTacticTable (loader:
// ai/playerbot/BossTacticsMgr.cpp). Lookups never touch the database. A row
// is "active" while a live creature with its boss entry attacks the bot's
// side; the strategy "boss tactics" then applies it.
//
// Pattern: mod-playerbots' per-map raid strategies (trigger -> action per
// boss mechanic) reimplemented as data rows; see docs/PROVENANCE.md.
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

#include "RearFlankPolicy.h"

namespace ai
{
    // Row kinds. The text form is what the `kind` column holds.
    enum class BossTacticKind : std::uint8_t
    {
        None = 0,
        Interrupt = 1,  // interrupt `spell_id` while the caster casts it
        KillFirst = 2,  // DPS bots kill `target_entry` before anything else; lowest `priority` first
        Behind = 3,     // non-tank bots stay out of the boss's front within `param` yd (frontal cleave)
    };

    inline BossTacticKind ParseBossTacticKind(const std::string& text)
    {
        if (text == "interrupt")
            return BossTacticKind::Interrupt;
        if (text == "kill_first")
            return BossTacticKind::KillFirst;
        if (text == "behind")
            return BossTacticKind::Behind;
        return BossTacticKind::None;
    }

    struct BossTacticRow
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;    // creature whose presence in the fight activates the row
        BossTacticKind kind = BossTacticKind::None;
        std::uint32_t targetEntry = 0;  // creature the row is about; 0 = the boss itself
        std::uint32_t spellId = 0;      // interrupt: the spell
        std::uint8_t priority = 0;      // kill_first: lower is killed first
        float param = 0.0f;             // behind: radius in yards
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
