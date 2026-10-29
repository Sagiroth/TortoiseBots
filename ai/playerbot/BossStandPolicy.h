#pragma once

// Per-boss stand sets and their pure decision rules (opt-in with the "boss
// tactics" strategy, see docs/play/boss-tactics.md).
//
// The rows live in the world table `ai_boss_stand`, keyed by map + boss
// creature entry, and are read ONCE at startup into BossStandTable (loader:
// ai/playerbot/BossStandMgr.cpp). Lookups never touch the database. A row
// is a standing point for bots of one role while the boss is near the row's
// anchor, optionally wears the row's aura and optionally is casting the row's
// window spell. When no row applies the bots play exactly as before.
//
// One boss entry holds several anchors (the places the boss stops at); each
// anchor holds, per role and window spell, a few points on one or two sides.
// A point seats up to `slots` bots in a line (dx, dy) at kBossStandSpacing
// from each other. The assignment of bots to points is a pure function of the
// group's positions, so every bot reaches the same plan on its own.
//
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_boss_stand_policy.cpp. The table has the
// same lifecycle as BossTacticTable (BossTacticsPolicy.h): one loading thread
// adds rows, calls Seal() once, and readers see nothing before the seal.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ai
{
    // The role a row is written for. The text form is what the `role` column holds.
    enum class StandRole : std::uint8_t
    {
        None = 0,
        Ranged = 1,
        Healer = 2,
        Melee = 3,
    };

    inline StandRole ParseStandRole(const std::string& text)
    {
        if (text == "ranged")
            return StandRole::Ranged;
        if (text == "healer")
            return StandRole::Healer;
        if (text == "melee")
            return StandRole::Melee;
        return StandRole::None;
    }

    // The role of a bot as the rows see it. A tank is never moved as a caster,
    // a healer is ranged too and counts as a healer.
    inline StandRole StandRoleOf(bool tank, bool healer, bool ranged)
    {
        if (tank)
            return StandRole::Melee;
        if (healer)
            return StandRole::Healer;
        return ranged ? StandRole::Ranged : StandRole::Melee;
    }

    // Yards between two bots seated on one point: Fireball's splash is 8 yd.
    constexpr float kBossStandSpacing = 9.0f;
    // A bot this close to its slot (2D) has arrived and stays put.
    constexpr float kBossStandArrived = 2.0f;
    // The most bots one point can be asked to seat.
    constexpr std::uint8_t kBossStandMaxSlots = 8;
    // The default count a point seats.
    constexpr std::uint8_t kBossStandDefaultSlots = 5;

    struct BossStandRow
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;
        std::uint32_t anchor = 0;       // id of the anchor within the boss entry; rows of one id share the anchor below
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        float anchorR = 0.0f;           // the boss is "at" the anchor within this many yards (2D)
        StandRole role = StandRole::None;
        std::uint32_t windowSpell = 0;  // 0: always; else only while the boss is casting this spell
        std::uint32_t aura = 0;         // 0: none; else the boss has to wear this aura
        std::uint8_t side = 0;          // 0 or 1: the side of the boss's lane the point lies on
        std::uint8_t seq = 0;           // the point's order within its side
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;                 // the floor at the point (the caller snaps it to the ground again)
        float dx = 0.0f;                // the unit direction the slots line up in; (0, 0): straight out from the anchor
        float dy = 0.0f;
        std::uint8_t slots = kBossStandDefaultSlots;
    };

    class BossStandTable
    {
    public:
        // Loading only. False after Seal(), for an unusable row, for a row of
        // an anchor id that disagrees with the first row of that id, or when the
        // (map, boss, anchor, role, window, side, seq) key is already there: the
        // first row wins, as it would under the table's primary key.
        bool Add(const BossStandRow& row)
        {
            if (sealed_.load(std::memory_order_acquire))
                return false;
            if (row.map == 0 || row.bossEntry == 0 || row.role == StandRole::None)
                return false;
            if (!(row.anchorR > 0.0f) || row.side > 1)
                return false;
            if (row.slots == 0 || row.slots > kBossStandMaxSlots)
                return false;
            std::vector<BossStandRow>& rows = rows_[Key(row.map, row.bossEntry)];
            for (const BossStandRow& have : rows)
            {
                if (have.anchor == row.anchor &&
                    (have.anchorX != row.anchorX || have.anchorY != row.anchorY || have.anchorR != row.anchorR))
                    return false;
                if (have.anchor == row.anchor && have.role == row.role && have.windowSpell == row.windowSpell &&
                    have.side == row.side && have.seq == row.seq)
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
        const std::vector<BossStandRow>* Rows(std::uint32_t map, std::uint32_t bossEntry) const
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

        std::unordered_map<std::uint64_t, std::vector<BossStandRow>> rows_;
        std::unordered_map<std::uint32_t, bool> maps_;
        std::size_t size_ = 0;
        std::atomic<bool> sealed_{false};
    };

    // Whether a row applies to a boss at (bossX, bossY): it is at the anchor
    // (2D, on the radius counts), wears the row's aura when the row names one,
    // and is casting the row's window spell when the row names one.
    inline bool StandRowActive(const BossStandRow& row, float bossX, float bossY, bool bossHasAura,
                               std::uint32_t castingSpell)
    {
        const float dx = bossX - row.anchorX;
        const float dy = bossY - row.anchorY;
        if (dx * dx + dy * dy > row.anchorR * row.anchorR)
            return false;
        if (row.aura != 0 && !bossHasAura)
            return false;
        if (row.windowSpell != 0 && castingSpell != row.windowSpell)
            return false;
        return true;
    }

    // The points a bot of `role` can stand at for this boss now: the active
    // rows of the role, of the anchor nearest to the boss (rows of another
    // anchor that also holds the boss are dropped). Empty means no row
    // applies and nothing changes. `hasAura` answers whether the boss wears an
    // aura id.
    template <typename HasAura>
    std::vector<const BossStandRow*> ActiveStandPoints(const std::vector<BossStandRow>& rows, StandRole role,
                                                       float bossX, float bossY, std::uint32_t castingSpell,
                                                       HasAura hasAura)
    {
        std::vector<const BossStandRow*> out;
        bool found = false;
        std::uint32_t anchor = 0;
        float bestDistance = 0.0f;
        for (const BossStandRow& row : rows)
        {
            if (row.role != role || !StandRowActive(row, bossX, bossY, row.aura != 0 && hasAura(row.aura), castingSpell))
                continue;
            const float dx = bossX - row.anchorX;
            const float dy = bossY - row.anchorY;
            const float distance = dx * dx + dy * dy;
            if (!found || distance < bestDistance)
            {
                found = true;
                anchor = row.anchor;
                bestDistance = distance;
            }
        }
        if (!found)
            return out;
        for (const BossStandRow& row : rows)
        {
            if (row.anchor == anchor && row.role == role &&
                StandRowActive(row, bossX, bossY, row.aura != 0 && hasAura(row.aura), castingSpell))
                out.push_back(&row);
        }
        return out;
    }

    // A bot of the stand set, as the assignment sees it.
    struct StandBot
    {
        std::uint64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct StandSlot
    {
        bool valid = false;
        std::size_t point = 0;  // index into the points handed to AssignStandSlot
        unsigned slot = 0;      // the bot's place on the point, 0 first
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    namespace stand_detail
    {
        inline float Dist2(float ax, float ay, float bx, float by)
        {
            return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
        }

        // The slot's position: `slot` * spacing along the row's line from its point.
        inline void SlotPosition(const BossStandRow& row, unsigned slot, float& x, float& y)
        {
            float dx = row.dx;
            float dy = row.dy;
            float length = std::sqrt(dx * dx + dy * dy);
            if (!(length > 0.0001f))
            {
                dx = row.x - row.anchorX;
                dy = row.y - row.anchorY;
                length = std::sqrt(dx * dx + dy * dy);
            }
            if (!(length > 0.0001f))
            {
                dx = 1.0f;
                dy = 0.0f;
                length = 1.0f;
            }
            x = row.x + dx / length * kBossStandSpacing * static_cast<float>(slot);
            y = row.y + dy / length * kBossStandSpacing * static_cast<float>(slot);
        }

        // The nearest point of a side that still has a free slot, by the bot's
        // 2D distance to the point; points.size() when the side is full.
        inline std::size_t NearestFree(const std::vector<const BossStandRow*>& points, const std::vector<unsigned>& used,
                                       std::uint8_t side, float botX, float botY)
        {
            std::size_t best = points.size();
            float bestDistance = 0.0f;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                if (points[i]->side != side || used[i] >= points[i]->slots)
                    continue;
                const float distance = Dist2(botX, botY, points[i]->x, points[i]->y);
                if (best == points.size() || distance < bestDistance)
                {
                    best = i;
                    bestDistance = distance;
                }
            }
            return best;
        }

        // The distance from a bot to the nearest point of a side; negative when
        // the side has no point.
        inline float SideDistance(const std::vector<const BossStandRow*>& points, std::uint8_t side, float botX, float botY)
        {
            bool any = false;
            float best = 0.0f;
            for (const BossStandRow* point : points)
            {
                if (point->side != side)
                    continue;
                const float distance = std::sqrt(Dist2(botX, botY, point->x, point->y));
                if (!any || distance < best)
                {
                    any = true;
                    best = distance;
                }
            }
            return any ? best : -1.0f;
        }
    }

    // The slot of bot `selfId` among `bots` (every bot of the set, self
    // included) on `points` (ActiveStandPoints). The bots are seated in id
    // order, so every bot computes the same plan whatever its own id:
    //  - a bot takes the side whose nearest point is nearer to it (it does not
    //    cross the lane for its point); a tie goes to the side with fewer bots
    //    seated so far, then to side 0;
    //  - on its side it takes the nearest point with a free slot; a full side
    //    sends it to the other side's nearest free point;
    //  - when every point is full the bot gets nothing (valid false) and keeps
    //    the behaviour it had.
    inline StandSlot AssignStandSlot(const std::vector<const BossStandRow*>& points, const std::vector<StandBot>& bots,
                                     std::uint64_t selfId)
    {
        StandSlot result;
        if (points.empty())
            return result;

        std::vector<std::size_t> order(bots.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return bots[a].id < bots[b].id; });

        std::vector<unsigned> used(points.size(), 0u);
        unsigned seated[2] = { 0u, 0u };
        for (std::size_t index : order)
        {
            const StandBot& bot = bots[index];
            const float near0 = stand_detail::SideDistance(points, 0, bot.x, bot.y);
            const float near1 = stand_detail::SideDistance(points, 1, bot.x, bot.y);
            std::uint8_t side;
            if (near0 < 0.0f)
                side = 1;
            else if (near1 < 0.0f)
                side = 0;
            else if (std::fabs(near0 - near1) < 0.5f)
                side = seated[1] < seated[0] ? 1 : 0;
            else
                side = near1 < near0 ? 1 : 0;

            std::size_t pick = stand_detail::NearestFree(points, used, side, bot.x, bot.y);
            if (pick == points.size())
            {
                side = side == 0 ? 1 : 0;
                pick = stand_detail::NearestFree(points, used, side, bot.x, bot.y);
            }
            if (pick == points.size())
                continue;  // every point is full: this bot is not seated

            const unsigned slot = used[pick]++;
            ++seated[side];
            if (bot.id == selfId)
            {
                result.valid = true;
                result.point = pick;
                result.slot = slot;
                stand_detail::SlotPosition(*points[pick], slot, result.x, result.y);
                result.z = points[pick]->z;
                return result;
            }
        }
        return result;
    }

    // Whether a bot at (botX, botY) is at its slot already.
    inline bool StandArrived(float botX, float botY, float slotX, float slotY)
    {
        return stand_detail::Dist2(botX, botY, slotX, slotY) <= kBossStandArrived * kBossStandArrived;
    }
}
