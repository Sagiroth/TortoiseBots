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
#include <functional>
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

    // How a row's window opens. `Cast`: the boss is casting the window spell (its current generic spell). A boss whose
    // spell is cast triggered never shows it as a current spell, so `StoppedNoTarget` opens the
    // window on the state the cast leaves: the boss wears the row's aura, stands still and has no target.
    enum class StandSignal : std::uint8_t
    {
        None = 0,
        Cast = 1,
        StoppedNoTarget = 2,
    };

    inline StandSignal ParseStandSignal(const std::string& text)
    {
        if (text == "cast")
            return StandSignal::Cast;
        if (text == "stopped_no_target")
            return StandSignal::StoppedNoTarget;
        return StandSignal::None;
    }

    // Yards between two bots seated on one point: Fireball's splash is 8 yd.
    constexpr float kBossStandSpacing = 9.0f;
    // A bot this close to its slot (2D) has arrived and stays put.
    constexpr float kBossStandArrived = 2.0f;
    // The most bots one point can be asked to seat.
    constexpr std::uint8_t kBossStandMaxSlots = 8;
    // The default count a point seats.
    constexpr std::uint8_t kBossStandDefaultSlots = 5;
    // How long a seat taken in a window outlives the window spell's cast by default (see StandHold).
    constexpr std::uint32_t kBossStandDefaultHoldMs = 8000;
    // The bounds a row's hold time has to keep: a hold that is too long disables the caster reach and the reactive
    // dodge for a bot for as long as it lasts.
    constexpr std::uint32_t kBossStandMinHoldMs = 1000;
    constexpr std::uint32_t kBossStandMaxHoldMs = 15000;
    // A bot counts toward the seats of an anchor when it is in the fight with the boss within this many yards of it.
    constexpr float kBossStandMemberRange = 80.0f;

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
        // A window row's seat outlives the cast: until the boss is within the radius of anchor `releaseAnchor` (the
        // far end of its lane; 0: none) or `holdMs` after the cast ended, whichever comes first.
        std::uint32_t holdMs = kBossStandDefaultHoldMs;
        std::uint32_t releaseAnchor = 0;
        StandSignal windowSignal = StandSignal::Cast;
    };

    // A boss whose whole stand set the seal refused.
    struct BossStandRefused
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;
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
            if (row.releaseAnchor != 0 && row.releaseAnchor == row.anchor)
                return false;
            if (row.holdMs < kBossStandMinHoldMs || row.holdMs > kBossStandMaxHoldMs)
                return false;
            // A signal that opens the window has a window to open and an aura to read it by.
            if (row.windowSignal == StandSignal::None)
                return false;
            if (row.windowSignal == StandSignal::StoppedNoTarget && (row.aura == 0 || row.windowSpell == 0))
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

        // Seals the table. Before the seal, the rows are checked against each other: a release anchor has to name an
        // anchor of the same map and boss, or that boss's whole stand set is refused (dropped; Refused() names it).
        // Readers see nothing before the seal.
        void Seal()
        {
            if (sealed_.load(std::memory_order_acquire))
                return;
            for (auto it = rows_.begin(); it != rows_.end();)
            {
                if (ReleaseAnchorsResolve(it->second))
                {
                    ++it;
                    continue;
                }
                BossStandRefused refused;
                refused.map = static_cast<std::uint32_t>(it->first >> 32);
                refused.bossEntry = static_cast<std::uint32_t>(it->first & 0xFFFFFFFFu);
                refused_.push_back(refused);
                size_ -= it->second.size();
                it = rows_.erase(it);
            }
            std::sort(refused_.begin(), refused_.end(), [](const BossStandRefused& a, const BossStandRefused& b)
                      { return a.map != b.map ? a.map < b.map : a.bossEntry < b.bossEntry; });
            maps_.clear();
            for (const auto& entry : rows_)
                maps_[static_cast<std::uint32_t>(entry.first >> 32)] = true;
            sealed_.store(true, std::memory_order_release);
        }

        // The boss stand sets the seal refused (read after the seal, by the loader's log).
        const std::vector<BossStandRefused>& Refused() const { return refused_; }
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

        // Every release anchor of one boss's rows names an anchor id some row of the same boss holds.
        static bool ReleaseAnchorsResolve(const std::vector<BossStandRow>& rows)
        {
            for (const BossStandRow& row : rows)
            {
                if (row.releaseAnchor == 0)
                    continue;
                bool found = false;
                for (const BossStandRow& other : rows)
                {
                    if (other.anchor == row.releaseAnchor)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        std::vector<BossStandRefused> refused_;
        std::unordered_map<std::uint64_t, std::vector<BossStandRow>> rows_;
        std::unordered_map<std::uint32_t, bool> maps_;
        std::size_t size_ = 0;
        std::atomic<bool> sealed_{false};
    };

    // Whether a row applies to a boss at (bossX, bossY): it is at the anchor
    // (2D, on the radius counts), wears the row's aura when the row names one,
    // and its window is open when the row names a window spell: the boss is
    // casting it, or (a row whose signal is StoppedNoTarget) it stands still
    // with no target. `stoppedNoTarget` is that state, read by the caller.
    inline bool StandRowActive(const BossStandRow& row, float bossX, float bossY, bool bossHasAura,
                               std::uint32_t castingSpell, bool stoppedNoTarget = false)
    {
        const float dx = bossX - row.anchorX;
        const float dy = bossY - row.anchorY;
        if (dx * dx + dy * dy > row.anchorR * row.anchorR)
            return false;
        if (row.aura != 0 && !bossHasAura)
            return false;
        if (row.windowSpell != 0 && castingSpell != row.windowSpell &&
            !(row.windowSignal == StandSignal::StoppedNoTarget && stoppedNoTarget))
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
                                                       HasAura hasAura, bool stoppedNoTarget = false)
    {
        std::vector<const BossStandRow*> out;
        bool found = false;
        std::uint32_t anchor = 0;
        float bestDistance = 0.0f;
        for (const BossStandRow& row : rows)
        {
            if (row.role != role ||
                !StandRowActive(row, bossX, bossY, row.aura != 0 && hasAura(row.aura), castingSpell, stoppedNoTarget))
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
                StandRowActive(row, bossX, bossY, row.aura != 0 && hasAura(row.aura), castingSpell, stoppedNoTarget))
                out.push_back(&row);
        }
        return out;
    }

    // The rows of one anchor for a role, whatever their window, aura or the boss's place: the seats a hold keeps.
    inline std::vector<const BossStandRow*> HeldStandPoints(const std::vector<BossStandRow>& rows, StandRole role,
                                                            std::uint32_t anchor)
    {
        std::vector<const BossStandRow*> out;
        for (const BossStandRow& row : rows)
        {
            if (row.anchor == anchor && row.role == role)
                out.push_back(&row);
        }
        return out;
    }

    // Whether the boss is within the radius of anchor `releaseAnchor` (2D, on the radius counts): the sweep that
    // followed the cast has reached the lane's far end. 0 and an anchor the rows do not hold never reach.
    inline bool StandReleaseAnchorReached(const std::vector<BossStandRow>& rows, std::uint32_t releaseAnchor,
                                          float bossX, float bossY)
    {
        if (releaseAnchor == 0)
            return false;
        for (const BossStandRow& row : rows)
        {
            if (row.anchor != releaseAnchor)
                continue;
            const float dx = bossX - row.anchorX;
            const float dy = bossY - row.anchorY;
            return dx * dx + dy * dy <= row.anchorR * row.anchorR;
        }
        return false;
    }

    // What a bot remembers of a window it was seated in, so that the seat outlives the cast. The breath's links go
    // on firing along the lane for seconds after the cast ends; a bot that left its seat the moment the window
    // closed would walk back under the boss, into the lane.
    struct StandHold
    {
        bool armed = false;
        std::uint32_t bossEntry = 0;
        std::uint32_t anchor = 0;          // the anchor of the window the bot was last seated in
        std::uint32_t releaseAnchor = 0;
        std::uint32_t holdMs = 0;
        std::uint32_t lastOpenMs = 0;      // the last time the window was open: the cast's end to within a tick
    };

    inline void StandHoldArm(StandHold& hold, const BossStandRow& row, std::uint32_t nowMs)
    {
        hold.armed = true;
        hold.bossEntry = row.bossEntry;
        hold.anchor = row.anchor;
        hold.releaseAnchor = row.releaseAnchor;
        hold.holdMs = row.holdMs;
        hold.lastOpenMs = nowMs;
    }

    // Whether a hold still seats: armed, less than holdMs since the window was last open (wrap-safe), and the boss
    // has not reached the release anchor.
    inline bool StandHoldActive(const StandHold& hold, std::uint32_t nowMs, bool releaseReached)
    {
        return hold.armed && !releaseReached && static_cast<std::uint32_t>(nowMs - hold.lastOpenMs) < hold.holdMs;
    }

    // The points a bot of `role` stands at now: the open window's (ActiveStandPoints; this arms or refreshes the
    // hold, and a window of another anchor re-seats from that anchor), else the held anchor's while the hold lasts,
    // else none (the hold is cleared). `rows` are the rows of one boss entry.
    template <typename HasAura>
    std::vector<const BossStandRow*> StandPointsNow(const std::vector<BossStandRow>& rows, StandRole role, float bossX,
                                                    float bossY, std::uint32_t castingSpell, HasAura hasAura,
                                                    StandHold& hold, std::uint32_t nowMs, bool stoppedNoTarget = false)
    {
        std::vector<const BossStandRow*> points =
            ActiveStandPoints(rows, role, bossX, bossY, castingSpell, hasAura, stoppedNoTarget);
        if (!points.empty())
        {
            StandHoldArm(hold, *points[0], nowMs);
            return points;
        }
        if (!hold.armed)
            return points;
        const bool sameBoss = !rows.empty() && rows[0].bossEntry == hold.bossEntry;
        if (!sameBoss ||
            !StandHoldActive(hold, nowMs, StandReleaseAnchorReached(rows, hold.releaseAnchor, bossX, bossY)))
        {
            hold = StandHold();
            return points;
        }
        return HeldStandPoints(rows, role, hold.anchor);
    }

    // A group member of the bot's role, as the seat selection sees it. `engaged`: the boss has it on its threat list
    // (the boss is in the fight with this bot); `holdsSameAnchor`: it already holds a stand for this boss and anchor.
    struct StandCandidate
    {
        std::uint64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
        bool engaged = false;
        bool holdsSameAnchor = false;
    };

    // Whether a member takes part in the seat assignment of an anchor: it holds a stand for the same boss and anchor,
    // or it is in the fight with the boss within kBossStandMemberRange of the anchor. A bot elsewhere in the
    // instance, or one the boss is not fighting, reserves no seat.
    inline bool StandMemberCounts(const StandCandidate& member, float anchorX, float anchorY)
    {
        if (member.holdsSameAnchor)
            return true;
        const float dx = member.x - anchorX;
        const float dy = member.y - anchorY;
        return member.engaged && dx * dx + dy * dy <= kBossStandMemberRange * kBossStandMemberRange;
    }

    // A bot of the stand set, as the assignment sees it.
    struct StandBot
    {
        std::uint64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    // The bots that share the seats: the members that count (StandMemberCounts), the same rule for every bot that
    // evaluates the plan, so all of them compute the same one. A bot that does not count is absent from the result and
    // AssignStandSlot gives it nothing.
    inline std::vector<StandBot> SelectStandBots(const std::vector<StandCandidate>& members, float anchorX, float anchorY)
    {
        std::vector<StandBot> out;
        for (const StandCandidate& member : members)
        {
            if (!StandMemberCounts(member, anchorX, anchorY))
                continue;
            StandBot bot;
            bot.id = member.id;
            bot.x = member.x;
            bot.y = member.y;
            out.push_back(bot);
        }
        return out;
    }

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

    // The lane of an anchor as a vector from the anchor to its release anchor (the far end of the boss's flight):
    // what "the side of the lane" is measured against. Invalid when the point names no release anchor, or no row
    // holds it.
    struct StandLane
    {
        bool valid = false;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    inline StandLane StandLaneOf(const std::vector<BossStandRow>& rows, const BossStandRow& point)
    {
        StandLane lane;
        if (point.releaseAnchor == 0)
            return lane;
        for (const BossStandRow& row : rows)
        {
            if (row.anchor != point.releaseAnchor)
                continue;
            lane.dx = row.anchorX - point.anchorX;
            lane.dy = row.anchorY - point.anchorY;
            lane.valid = lane.dx * lane.dx + lane.dy * lane.dy > 0.0001f;
            return lane;
        }
        return lane;
    }

    // Yards a bot covers in a 5 s breath cast at run speed (7 yd/s): a seat further than this is still taken, but the
    // plan seats as many bots as it can within it.
    constexpr float kBossStandReach = 35.0f;
    // Cost added to a seat beyond kBossStandReach, and the cost of leaving a bot unseated (a side with fewer seats than
    // bots): both dwarf any real walk, so the count within reach is decided first and the total walk second.
    constexpr double kBossStandFarPenalty = 1000.0;
    constexpr double kBossStandUnseatedCost = 1000000.0;

    // One bot's seat in a window's plan.
    struct StandSeat
    {
        std::uint64_t id = 0;
        bool valid = false;  // false: its side had no free seat (it keeps the reactive dodge)
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // The side of the lane a bot stands on (0 or 1), as AssignStandSlot reads it; `tie` is set within half a yard
    // of the lane line (or of equal distance to both sides' points when no lane is known).
    inline std::uint8_t StandSideOf(const std::vector<const BossStandRow*>& points, const StandLane& lane,
                                    const StandBot& bot, bool& tie)
    {
        tie = false;
        const float laneLength = std::sqrt(lane.dx * lane.dx + lane.dy * lane.dy);
        if (lane.valid && laneLength > 0.0001f)
        {
            const float lateral =
                (lane.dx * (bot.y - points[0]->anchorY) - lane.dy * (bot.x - points[0]->anchorX)) / laneLength;
            tie = std::fabs(lateral) < 0.5f;
            return lateral >= 0.0f ? 0 : 1;
        }
        const float near0 = stand_detail::SideDistance(points, 0, bot.x, bot.y);
        const float near1 = stand_detail::SideDistance(points, 1, bot.x, bot.y);
        if (near0 < 0.0f)
            return 1;
        if (near1 < 0.0f)
            return 0;
        tie = std::fabs(near0 - near1) < 0.5f;
        return near1 < near0 ? 1 : 0;
    }

    namespace stand_detail
    {
        // Min-cost assignment (Hungarian, O(n^3)) of rows to columns of a square cost matrix: assignment[row] = column.
        // Deterministic for a given matrix: rows and columns are scanned in index order and a tie keeps the lower index.
        inline std::vector<std::size_t> MinCostSeats(const std::vector<std::vector<double>>& cost)
        {
            const std::size_t n = cost.size();
            const double inf = 1e18;
            std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
            std::vector<std::size_t> p(n + 1, 0), way(n + 1, 0);
            for (std::size_t i = 1; i <= n; ++i)
            {
                p[0] = i;
                std::size_t j0 = 0;
                std::vector<double> minv(n + 1, inf);
                std::vector<bool> used(n + 1, false);
                do
                {
                    used[j0] = true;
                    const std::size_t i0 = p[j0];
                    double delta = inf;
                    std::size_t j1 = 0;
                    for (std::size_t j = 1; j <= n; ++j)
                    {
                        if (used[j])
                            continue;
                        const double cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
                        if (cur < minv[j])
                        {
                            minv[j] = cur;
                            way[j] = j0;
                        }
                        if (minv[j] < delta)
                        {
                            delta = minv[j];
                            j1 = j;
                        }
                    }
                    for (std::size_t j = 0; j <= n; ++j)
                    {
                        if (used[j])
                        {
                            u[p[j]] += delta;
                            v[j] -= delta;
                        }
                        else
                            minv[j] -= delta;
                    }
                    j0 = j1;
                } while (p[j0] != 0);
                do
                {
                    const std::size_t j1 = way[j0];
                    p[j0] = p[j1];
                    j0 = j1;
                } while (j0 != 0);
            }
            std::vector<std::size_t> assignment(n, 0);
            for (std::size_t j = 1; j <= n; ++j)
                if (p[j] != 0)
                    assignment[p[j] - 1] = j - 1;
            return assignment;
        }

        // A seat's spot: one slot of one of a side's points.
        struct StandSpot
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        // The spots of a side: every slot of every point of that side, points in `points` order, slots 0..slots-1.
        inline std::vector<StandSpot> SideSpots(const std::vector<const BossStandRow*>& points, std::uint8_t side)
        {
            std::vector<StandSpot> spots;
            for (const BossStandRow* point : points)
            {
                if (point->side != side)
                    continue;
                for (unsigned slot = 0; slot < point->slots; ++slot)
                {
                    StandSpot spot;
                    SlotPosition(*point, slot, spot.x, spot.y);
                    spot.z = point->z;
                    spots.push_back(spot);
                }
            }
            return spots;
        }

        // Seats `bots` (in id order) among `spots`, min-cost: the cost is the 2D distance plus kBossStandFarPenalty
        // beyond kBossStandReach, and a bot the spots run out for stays unseated (kBossStandUnseatedCost). The seat of
        // bots[r] is written to plan[index[r]], left invalid when that bot is the one left out.
        inline void SeatBots(std::vector<StandSeat>& plan, const std::vector<std::size_t>& index,
                             const std::vector<StandBot>& bots, const std::vector<StandSpot>& spots)
        {
            if (bots.empty() || spots.empty())
                return;
            const std::size_t n = std::max(bots.size(), spots.size());
            std::vector<std::vector<double>> cost(n, std::vector<double>(n, 0.0));
            for (std::size_t r = 0; r < bots.size(); ++r)
            {
                for (std::size_t c = 0; c < n; ++c)
                {
                    if (c >= spots.size())
                    {
                        cost[r][c] = kBossStandUnseatedCost;
                        continue;
                    }
                    const double d = std::sqrt(Dist2(bots[r].x, bots[r].y, spots[c].x, spots[c].y));
                    cost[r][c] = d + (d > kBossStandReach ? kBossStandFarPenalty : 0.0);
                }
            }
            const std::vector<std::size_t> pick = MinCostSeats(cost);
            for (std::size_t r = 0; r < bots.size(); ++r)
            {
                if (pick[r] >= spots.size())
                    continue;
                StandSeat& seat = plan[index[r]];
                seat.valid = true;
                seat.x = spots[pick[r]].x;
                seat.y = spots[pick[r]].y;
                seat.z = spots[pick[r]].z;
            }
        }

        // One side's seats, min-cost: its members (`members` indexes into `bots`, in id order) to every slot of the
        // side's points (SideSpots); a member the side's spots run out for leaves its plan entry invalid (it keeps the
        // reactive dodge). Writes plan[members[r]].
        inline void SeatSide(std::vector<StandSeat>& plan, const std::vector<StandBot>& bots,
                             const std::vector<std::size_t>& members, const std::vector<const BossStandRow*>& points,
                             std::uint8_t side)
        {
            if (members.empty())
                return;
            std::vector<StandBot> sideBots;
            sideBots.reserve(members.size());
            for (std::size_t index : members)
                sideBots.push_back(bots[index]);
            SeatBots(plan, members, sideBots, SideSpots(points, side));
        }
    }

    // The window's seat plan: one StandSeat per bot of `bots`, in ascending id order.
    //  - Side: StandSideOf. Bots on the lane line (tie) are placed after the others, in id order, each on the side
    //    with fewer bots so far (side 0 on equal counts). A bot never gets a seat on the other side.
    //  - Seats of a side: every slot of every point of that side (SlotPosition), points in `points` order, slots 0..n-1.
    //  - Per side, a min-cost assignment of its bots (id order) to its seats: the cost is the 2D distance, plus
    //    kBossStandFarPenalty beyond kBossStandReach; a side with more bots than seats leaves the rest unseated
    //    (kBossStandUnseatedCost). So the plan seats as many bots within reach as it can, then walks the least in total,
    //    and a seat beyond reach is still taken.
    inline std::vector<StandSeat> PlanStandSeats(const std::vector<const BossStandRow*>& points,
                                                 const std::vector<StandBot>& bots, const StandLane& lane)
    {
        std::vector<StandBot> sorted(bots);
        std::stable_sort(sorted.begin(), sorted.end(), [](const StandBot& a, const StandBot& b) { return a.id < b.id; });
        std::vector<StandSeat> plan(sorted.size());
        for (std::size_t i = 0; i < sorted.size(); ++i)
            plan[i].id = sorted[i].id;
        if (points.empty())
            return plan;

        std::vector<std::uint8_t> side(sorted.size(), 0);
        std::vector<bool> tie(sorted.size(), false);
        std::size_t count[2] = { 0, 0 };
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            bool t = false;
            side[i] = StandSideOf(points, lane, sorted[i], t);
            tie[i] = t;
            if (!t)
                ++count[side[i]];
        }
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            if (!tie[i])
                continue;
            side[i] = count[1] < count[0] ? 1 : 0;
            ++count[side[i]];
        }

        for (std::uint8_t s = 0; s < 2; ++s)
        {
            std::vector<std::size_t> members;
            for (std::size_t i = 0; i < sorted.size(); ++i)
                if (side[i] == s)
                    members.push_back(i);
            stand_detail::SeatSide(plan, sorted, members, points, s);
        }
        return plan;
    }

    // The window's plan, grown by the members of `bots` it does not name yet; every bot the plan names is left alone,
    // so a window whose members are all named is a plain no-op (every bot asks for its seat each tick). Each newcomer
    // gets its side (StandSideOf; a tie goes to the side with fewer members, in id order), and a side's newcomers then
    // take that side's free spots together, min-cost (stand_detail::SeatBots): as many within kBossStandReach as the
    // spots allow, then the least total walk; seated one newcomer after another the last of them can be left a walk an
    // earlier one took. A spot is free while no VALID entry of the plan sits within 0.01 yd of it. Existing entries,
    // valid or not, are never changed or removed. Newcomers beyond the free spots are added with valid = false (they
    // keep the reactive dodge, as in PlanStandSeats). The plan stays in ascending id order.
    inline bool ExtendStandPlan(std::vector<StandSeat>& plan, const std::vector<const BossStandRow*>& points,
                                const std::vector<StandBot>& bots, const StandLane& lane)
    {
        if (points.empty())
            return false;
        const auto named = [&plan](std::uint64_t id)
        {
            for (const StandSeat& seat : plan)
                if (seat.id == id)
                    return true;
            return false;
        };
        bool newcomer = false;
        for (const StandBot& bot : bots)
        {
            if (named(bot.id))
                continue;
            newcomer = true;
            break;
        }
        if (!newcomer)
            return false;  // nothing to add: the plan is left as it is, unsorted

        std::vector<StandBot> sorted(bots);
        std::stable_sort(sorted.begin(), sorted.end(), [](const StandBot& a, const StandBot& b) { return a.id < b.id; });

        // The newcomers by side, in id order; the count that breaks a tie rises with every member placed, as in
        // PlanStandSeats.
        std::size_t count[2] = { 0, 0 };
        for (const StandSeat& seat : plan)
        {
            if (!seat.valid)
                continue;
            bool seatTie = false;
            ++count[StandSideOf(points, lane, { 0, seat.x, seat.y }, seatTie)];
        }
        std::vector<StandBot> sided[2];
        for (const StandBot& bot : sorted)
        {
            if (named(bot.id))
                continue;
            bool tie = false;
            std::uint8_t side = StandSideOf(points, lane, bot, tie);
            if (tie)
                side = count[1] < count[0] ? 1 : 0;
            ++count[side];
            sided[side].push_back(bot);
        }

        for (std::uint8_t s = 0; s < 2; ++s)
        {
            if (sided[s].empty())
                continue;
            std::vector<stand_detail::StandSpot> free;
            for (const stand_detail::StandSpot& spot : stand_detail::SideSpots(points, s))
            {
                bool held = false;
                for (const StandSeat& seat : plan)
                {
                    if (!seat.valid)
                        continue;
                    if (stand_detail::Dist2(seat.x, seat.y, spot.x, spot.y) <= 0.0001f)  // 0.01 yd
                    {
                        held = true;
                        break;
                    }
                }
                if (!held)
                    free.push_back(spot);
            }
            std::vector<std::size_t> index;
            index.reserve(sided[s].size());
            for (const StandBot& bot : sided[s])
            {
                StandSeat seat;
                seat.id = bot.id;
                index.push_back(plan.size());
                plan.push_back(seat);  // invalid until SeatBots hands it one of the side's free spots
            }
            stand_detail::SeatBots(plan, index, sided[s], free);
        }
        std::stable_sort(plan.begin(), plan.end(), [](const StandSeat& a, const StandSeat& b) { return a.id < b.id; });
        return true;
    }

    // How long a plan outlives its last read: longer than a tick gap, shorter than the 15 s between two windows.
    constexpr std::uint32_t kBossStandPlanIdleMs = 3000;

    inline bool StandPlanStale(std::uint32_t nowMs, std::uint32_t lastReadMs)
    {
        return static_cast<std::uint32_t>(nowMs - lastReadMs) >= kBossStandPlanIdleMs;
    }

    // What a window's plan is filed under: the boss instance (raw guid, map and instance id), the window's anchor,
    // the role and the window spell the row opens on. The raw guid alone names the spawn in the database, which a second
    // instance of the same boss shares, so each instance needs its own plan; two rows of one anchor and role can name
    // different window spells, and a plan built for one window must not seat a bot of the other. The group is its
    // leader's raw guid (0 for none): a bot outside the raid never reads or builds the raid's seats. Guarded by the
    // caller's lock.
    struct StandPlanKey
    {
        std::uint64_t boss = 0;
        std::uint32_t mapId = 0;
        std::uint32_t instanceId = 0;
        std::uint32_t anchor = 0;
        std::uint8_t role = 0;
        std::uint32_t windowSpell = 0;
        std::uint64_t group = 0;
        bool operator==(const StandPlanKey& other) const
        {
            return boss == other.boss && mapId == other.mapId && instanceId == other.instanceId &&
                   anchor == other.anchor && role == other.role && windowSpell == other.windowSpell &&
                   group == other.group;
        }
    };

    struct StandPlanKeyHash
    {
        std::size_t operator()(const StandPlanKey& key) const
        {
            std::size_t h = std::hash<std::uint64_t>()(key.boss);
            h ^= std::hash<std::uint32_t>()(key.mapId) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>()(key.instanceId) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>()(key.anchor) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint8_t>()(key.role) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint32_t>()(key.windowSpell) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::uint64_t>()(key.group) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    // The slot of bot `selfId` among `bots` (every bot of the set, self
    // included) on `points` (ActiveStandPoints). The bots are seated in id
    // order, so every bot computes the same plan whatever its own id:
    //  - a bot takes the side of the lane it stands on: the sign of
    //    cross(lane, anchor -> bot), side 0 being "+"; with no lane known, the
    //    side of the nearest point. Within half a yard of the lane line it goes
    //    to the side with fewer bots seated so far, then to side 0;
    //  - on its side it takes the nearest point with a free slot;
    //  - a side with no point or no free slot seats the bot nowhere (valid
    //    false): it never crosses the lane for a seat and keeps the behaviour
    //    it had (the reactive dodge).
    inline StandSlot AssignStandSlot(const std::vector<const BossStandRow*>& points, const std::vector<StandBot>& bots,
                                     std::uint64_t selfId, const StandLane& lane = StandLane())
    {
        StandSlot result;
        if (points.empty())
            return result;

        std::vector<std::size_t> order(bots.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return bots[a].id < bots[b].id; });

        const float laneLength = std::sqrt(lane.dx * lane.dx + lane.dy * lane.dy);
        std::vector<unsigned> used(points.size(), 0u);
        unsigned seated[2] = { 0u, 0u };
        for (std::size_t index : order)
        {
            const StandBot& bot = bots[index];
            std::uint8_t side;
            bool tie = false;
            if (lane.valid && laneLength > 0.0001f)
            {
                const float lateral =
                    (lane.dx * (bot.y - points[0]->anchorY) - lane.dy * (bot.x - points[0]->anchorX)) / laneLength;
                tie = std::fabs(lateral) < 0.5f;
                side = lateral >= 0.0f ? 0 : 1;
            }
            else
            {
                const float near0 = stand_detail::SideDistance(points, 0, bot.x, bot.y);
                const float near1 = stand_detail::SideDistance(points, 1, bot.x, bot.y);
                if (near0 < 0.0f)
                    side = 1;
                else if (near1 < 0.0f)
                    side = 0;
                else
                {
                    tie = std::fabs(near0 - near1) < 0.5f;
                    side = near1 < near0 ? 1 : 0;
                }
            }
            if (tie)
                side = seated[1] < seated[0] ? 1 : 0;

            const std::size_t pick = stand_detail::NearestFree(points, used, side, bot.x, bot.y);
            if (pick == points.size())
                continue;  // no free seat on the bot's own side: it is not seated

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

    // The ground under a seat: the probe starts above both the row's floor and the bot's own (a far seat's floor can
    // lie above the row z), and a probe that comes back unchanged found nothing, so the row's z stays.
    constexpr float kBossStandProbeAbove = 4.0f;

    inline float StandProbeZ(float rowZ, float botZ)
    {
        return (rowZ > botZ ? rowZ : botZ) + kBossStandProbeAbove;
    }

    inline float StandSnappedZ(float rowZ, float probeZ, float groundZ)
    {
        return groundZ == probeZ ? rowZ : groundZ;
    }

    // A seat the mover refuses this many times in a row is given up on (valid false for the bot: the reach and the
    // dodge take over).
    constexpr std::uint8_t kBossStandMaxMoveFailures = 3;

    struct StandFailure
    {
        float x = 0.0f;
        float y = 0.0f;
        std::uint8_t count = 0;
    };

    namespace stand_detail
    {
        inline bool SameSeat(const StandFailure& failure, float x, float y)
        {
            return Dist2(failure.x, failure.y, x, y) <= 1.0f;
        }
    }

    inline void StandNoteMoveFailure(StandFailure& failure, float x, float y)
    {
        if (failure.count == 0 || !stand_detail::SameSeat(failure, x, y))
        {
            failure.x = x;
            failure.y = y;
            failure.count = 0;
        }
        if (failure.count < 255)
            ++failure.count;
    }

    inline void StandNoteMoveSuccess(StandFailure& failure)
    {
        failure = StandFailure();
    }

    inline bool StandSeatGivenUp(const StandFailure& failure, float x, float y)
    {
        return failure.count >= kBossStandMaxMoveFailures && stand_detail::SameSeat(failure, x, y);
    }

    // Whether a bot at (botX, botY) is at its slot already.
    inline bool StandArrived(float botX, float botY, float slotX, float slotY)
    {
        return stand_detail::Dist2(botX, botY, slotX, slotY) <= kBossStandArrived * kBossStandArrived;
    }

    // A bot this close to its seat keeps the cast it is in the middle of; further out the walk stops it.
    constexpr float kBossStandKeepCastRange = 6.0f;

    inline bool StandShouldStopCast(float botX, float botY, float slotX, float slotY)
    {
        return stand_detail::Dist2(botX, botY, slotX, slotY) > kBossStandKeepCastRange * kBossStandKeepCastRange;
    }
}
