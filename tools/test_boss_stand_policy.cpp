// Standalone check for ai/playerbot/BossStandPolicy.h:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_boss_stand_policy.cpp -o /tmp/test_boss_stand_policy && /tmp/test_boss_stand_policy
#include "../ai/playerbot/BossStandPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static BossStandRow Row(std::uint32_t anchor, float ax, float ay, float ar, StandRole role, std::uint32_t window,
                        std::uint8_t side, std::uint8_t seq, float x, float y, float dx, float dy, std::uint8_t slots = 5,
                        std::uint32_t aura = 0)
{
    BossStandRow row;
    row.map = 249;
    row.bossEntry = 10184;
    row.anchor = anchor;
    row.anchorX = ax;
    row.anchorY = ay;
    row.anchorR = ar;
    row.role = role;
    row.windowSpell = window;
    row.aura = aura;
    row.side = side;
    row.seq = seq;
    row.x = x;
    row.y = y;
    row.z = -85.0f;
    row.dx = dx;
    row.dy = dy;
    row.slots = slots;
    return row;
}

static float Dist(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

static bool NoAura(std::uint32_t) { return false; }
static bool AllAuras(std::uint32_t) { return true; }

// A lane along x with the boss at the origin: side 0 is +y (two points), side 1 is -y (one point), all in lines
// along the lane.
static std::vector<BossStandRow> LaneRows()
{
    std::vector<BossStandRow> rows;
    rows.push_back(Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0));
    rows.push_back(Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 1, 50, 32, 1, 0));
    rows.push_back(Row(1, 0, 0, 8, StandRole::Ranged, 777, 1, 0, 0, -32, 1, 0));
    return rows;
}

static std::vector<const BossStandRow*> Points(const std::vector<BossStandRow>& rows)
{
    std::vector<const BossStandRow*> out;
    for (const BossStandRow& row : rows)
        out.push_back(&row);
    return out;
}

int main()
{
    std::cout << "Starting TortoiseBots boss stand policy tests...\n";

    // The numbers the data is laid out for (the migration's seats are 9 yd apart and path-checked at that spacing).
    CHECK(kBossStandSpacing == 9.0f);
    CHECK(kBossStandArrived == 2.0f);
    CHECK(kBossStandMaxSlots == 8);

    // Role text and role of a bot.
    CHECK(ParseStandRole("ranged") == StandRole::Ranged);
    CHECK(ParseStandRole("healer") == StandRole::Healer);
    CHECK(ParseStandRole("melee") == StandRole::Melee);
    CHECK(ParseStandRole("Ranged") == StandRole::None);
    CHECK(ParseStandRole("tank") == StandRole::None);
    CHECK(StandRoleOf(true, false, true) == StandRole::Melee);    // a tank is never moved as a caster
    CHECK(StandRoleOf(false, true, true) == StandRole::Healer);   // a healer is ranged too: healer first
    CHECK(StandRoleOf(false, false, true) == StandRole::Ranged);
    CHECK(StandRoleOf(false, false, false) == StandRole::Melee);

    // Table: the loader's rules.
    {
        BossStandTable table;
        CHECK(table.Add(Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0)));
        CHECK(!table.HasMap(249));  // nothing before the seal
        CHECK(table.Rows(249, 10184) == nullptr);
        // Same key again: the first row wins.
        CHECK(!table.Add(Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 9, 9, 1, 0)));
        // The same anchor id with other coordinates is a typo, not another anchor.
        CHECK(!table.Add(Row(1, 5, 0, 8, StandRole::Ranged, 777, 0, 1, 0, 32, 1, 0)));
        CHECK(!table.Add(Row(1, 0, 0, 9, StandRole::Ranged, 777, 0, 1, 0, 32, 1, 0)));
        // Unusable rows.
        CHECK(!table.Add(Row(2, 0, 0, 8, StandRole::None, 777, 0, 0, 0, 32, 1, 0)));   // unknown role
        CHECK(!table.Add(Row(2, 0, 0, 0, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0)));  // no anchor radius
        CHECK(!table.Add(Row(2, 0, 0, 8, StandRole::Ranged, 777, 2, 0, 0, 32, 1, 0)));  // only sides 0 and 1
        CHECK(!table.Add(Row(2, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0, 0)));  // a point seats nobody
        CHECK(!table.Add(Row(2, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0, kBossStandMaxSlots + 1)));
        BossStandRow noMap = Row(2, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0);
        noMap.map = 0;
        CHECK(!table.Add(noMap));
        BossStandRow noBoss = Row(2, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0);
        noBoss.bossEntry = 0;
        CHECK(!table.Add(noBoss));
        // A second anchor and another role are fine.
        CHECK(table.Add(Row(2, 40, 0, 8, StandRole::Ranged, 777, 0, 0, 40, 32, 1, 0)));
        CHECK(table.Add(Row(1, 0, 0, 8, StandRole::Healer, 777, 0, 0, 0, 40, 1, 0)));
        table.Seal();
        CHECK(table.Sealed());
        CHECK(table.Size() == 3);
        CHECK(table.HasMap(249));
        CHECK(!table.HasMap(1));
        CHECK(table.Rows(249, 10184) != nullptr);
        CHECK(table.Rows(249, 10184)->size() == 3);
        CHECK(table.Rows(249, 1) == nullptr);
        CHECK(!table.Add(Row(3, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0)));  // sealed
    }

    // Anchor, aura and window: every part has to hold.
    {
        const BossStandRow row = Row(1, 10, 20, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0, 5, 17131);
        CHECK(StandRowActive(row, 10, 20, true, 777));
        CHECK(StandRowActive(row, 10 + 8, 20, true, 777));     // on the radius
        CHECK(StandRowActive(row, 10 + 5, 20 + 6, true, 777)); // inside it, diagonal
        CHECK(!StandRowActive(row, 10 + 8.5f, 20, true, 777)); // outside the radius
        CHECK(!StandRowActive(row, 10 + 6, 20 + 6, true, 777));
        CHECK(!StandRowActive(row, 10, 20, false, 777));       // the aura is named and missing
        CHECK(!StandRowActive(row, 10, 20, true, 0));          // not casting the window spell
        CHECK(!StandRowActive(row, 10, 20, true, 778));        // casting another one
        // No aura named: the boss's auras do not matter. No window named: the row is continuous.
        const BossStandRow plain = Row(1, 10, 20, 8, StandRole::Ranged, 0, 0, 0, 0, 32, 1, 0, 5, 0);
        CHECK(StandRowActive(plain, 10, 20, false, 0));
        CHECK(StandRowActive(plain, 10, 20, false, 12345));
        CHECK(!StandRowActive(plain, 40, 20, false, 0));
    }

    // The points a bot of a role stands at: none without a match (nothing changes).
    {
        std::vector<BossStandRow> rows = LaneRows();
        rows.push_back(Row(1, 0, 0, 8, StandRole::Healer, 777, 0, 0, 0, 36, 1, 0));
        rows.push_back(Row(2, 100, 0, 8, StandRole::Ranged, 777, 0, 0, 100, 32, 1, 0));
        // At the first anchor, in the window: its three ranged points.
        CHECK(ActiveStandPoints(rows, StandRole::Ranged, 1, 0, 777, NoAura).size() == 3);
        // Healers see their own row only; melee none (only ranged and healers have rows).
        CHECK(ActiveStandPoints(rows, StandRole::Healer, 1, 0, 777, NoAura).size() == 1);
        CHECK(ActiveStandPoints(rows, StandRole::Melee, 1, 0, 777, NoAura).empty());
        // Not casting the window spell: nothing (C1's reach and the reactive dodge stay in charge).
        CHECK(ActiveStandPoints(rows, StandRole::Ranged, 1, 0, 0, NoAura).empty());
        CHECK(ActiveStandPoints(rows, StandRole::Ranged, 1, 0, 778, NoAura).empty());
        // Between the anchors: nothing.
        CHECK(ActiveStandPoints(rows, StandRole::Ranged, 50, 0, 777, NoAura).empty());
        // The second anchor picks its own row.
        const std::vector<const BossStandRow*> second = ActiveStandPoints(rows, StandRole::Ranged, 100, 3, 777, NoAura);
        CHECK(second.size() == 1);
        CHECK(second[0]->anchor == 2);
        // Anchors that overlap: the nearer one to the boss wins, its rows only.
        std::vector<BossStandRow> near = rows;
        near.push_back(Row(3, 6, 0, 8, StandRole::Ranged, 777, 0, 0, 6, -32, 1, 0));
        const std::vector<const BossStandRow*> nearest = ActiveStandPoints(near, StandRole::Ranged, 5, 0, 777, NoAura);
        CHECK(nearest.size() == 1);
        CHECK(nearest[0]->anchor == 3);
        // A nearer anchor of another role does not steal the choice.
        std::vector<BossStandRow> other = rows;
        other.push_back(Row(3, 6, 0, 8, StandRole::Healer, 777, 0, 0, 6, -32, 1, 0));
        const std::vector<const BossStandRow*> mine = ActiveStandPoints(other, StandRole::Ranged, 5, 0, 777, NoAura);
        CHECK(mine.size() == 3);
        CHECK(mine[0]->anchor == 1);
        // The aura is read through the callback.
        std::vector<BossStandRow> auraRows = { Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 1, 0, 5, 17131) };
        CHECK(ActiveStandPoints(auraRows, StandRole::Ranged, 0, 0, 777, NoAura).empty());
        CHECK(ActiveStandPoints(auraRows, StandRole::Ranged, 0, 0, 777, AllAuras).size() == 1);
        // No rows at all.
        CHECK(ActiveStandPoints(std::vector<BossStandRow>(), StandRole::Ranged, 0, 0, 777, AllAuras).empty());
    }

    // Slots: the nearest point on the bot's own side, spread along the line.
    {
        const std::vector<BossStandRow> rows = LaneRows();
        const std::vector<const BossStandRow*> points = Points(rows);

        // A lone bot on the +y side, near x 2: the first +y point, slot 0 on it.
        {
            std::vector<StandBot> bots = { { 1, 2, 10 } };
            StandSlot slot = AssignStandSlot(points, bots, 1);
            CHECK(slot.valid);
            CHECK(points[slot.point]->side == 0 && points[slot.point]->seq == 0);
            CHECK(slot.slot == 0);
            CHECK(std::fabs(slot.x - 0.0f) < 0.001f && std::fabs(slot.y - 32.0f) < 0.001f);
            CHECK(std::fabs(slot.z + 85.0f) < 0.001f);
        }
        // The same bot a long way along the lane takes the nearer +y point (seq 1).
        {
            std::vector<StandBot> bots = { { 1, 48, 10 } };
            StandSlot slot = AssignStandSlot(points, bots, 1);
            CHECK(slot.valid && points[slot.point]->seq == 1);
        }
        // A bot on the -y side stays on it: it never crosses the lane for its point.
        {
            std::vector<StandBot> bots = { { 1, 2, -10 } };
            StandSlot slot = AssignStandSlot(points, bots, 1);
            CHECK(slot.valid && points[slot.point]->side == 1);
            CHECK(std::fabs(slot.y + 32.0f) < 0.001f);
        }
        // Self not among the bots: nothing.
        {
            std::vector<StandBot> bots = { { 2, 2, 10 } };
            CHECK(!AssignStandSlot(points, bots, 1).valid);
            CHECK(!AssignStandSlot(points, std::vector<StandBot>(), 1).valid);
            CHECK(!AssignStandSlot(std::vector<const BossStandRow*>(), bots, 2).valid);
        }
        // Seven bots on the +y side: five fill the nearest point, two go to the next point of the same side.
        {
            std::vector<StandBot> bots;
            for (std::uint64_t id = 1; id <= 7; ++id)
                bots.push_back({ id, 2.0f, 10.0f });
            int onFirst = 0, onSecond = 0, mirrored = 0;
            for (std::uint64_t id = 1; id <= 7; ++id)
            {
                StandSlot slot = AssignStandSlot(points, bots, id);
                CHECK(slot.valid);
                if (points[slot.point]->side == 1)
                    ++mirrored;
                else if (points[slot.point]->seq == 0)
                    ++onFirst;
                else
                    ++onSecond;
            }
            CHECK(onFirst == 5 && onSecond == 2 && mirrored == 0);
        }
        // Past both +y points (5 + 5), the mirrored side takes the rest, then nobody is seated.
        {
            std::vector<StandBot> bots;
            for (std::uint64_t id = 1; id <= 17; ++id)
                bots.push_back({ id, 2.0f, 10.0f });
            int seated = 0, mirrored = 0, unseated = 0;
            for (std::uint64_t id = 1; id <= 17; ++id)
            {
                StandSlot slot = AssignStandSlot(points, bots, id);
                if (!slot.valid)
                    ++unseated;
                else
                {
                    ++seated;
                    if (points[slot.point]->side == 1)
                        ++mirrored;
                }
            }
            CHECK(seated == 15 && mirrored == 5 && unseated == 2);
        }
        // Spread: every seated bot is at least 9 yd from every other one.
        {
            std::vector<StandBot> bots;
            for (std::uint64_t id = 1; id <= 15; ++id)
                bots.push_back({ id, 2.0f + id, (id % 2) ? 10.0f : -10.0f });
            std::vector<StandSlot> slots;
            for (std::uint64_t id = 1; id <= 15; ++id)
            {
                StandSlot slot = AssignStandSlot(points, bots, id);
                CHECK(slot.valid);
                slots.push_back(slot);
            }
            for (std::size_t a = 0; a < slots.size(); ++a)
                for (std::size_t b = a + 1; b < slots.size(); ++b)
                    CHECK(Dist(slots[a].x, slots[a].y, slots[b].x, slots[b].y) >= kBossStandSpacing - 0.001f);
        }
        // Slot k stands k * spacing along the row's line from the point.
        {
            std::vector<StandBot> bots;
            for (std::uint64_t id = 1; id <= 3; ++id)
                bots.push_back({ id, 2.0f, 10.0f });
            for (std::uint64_t id = 1; id <= 3; ++id)
            {
                StandSlot slot = AssignStandSlot(points, bots, id);
                CHECK(slot.valid && points[slot.point]->seq == 0);
                CHECK(slot.slot == id - 1);
                CHECK(std::fabs(slot.x - (id - 1) * kBossStandSpacing) < 0.001f);
                CHECK(std::fabs(slot.y - 32.0f) < 0.001f);
            }
        }
        // The count limit of a row: slots 2 seat two bots, the third goes on.
        {
            std::vector<BossStandRow> small = LaneRows();
            small[0].slots = 2;
            const std::vector<const BossStandRow*> p = Points(small);
            std::vector<StandBot> bots = { { 1, 2, 10 }, { 2, 2, 10 }, { 3, 2, 10 } };
            CHECK(AssignStandSlot(p, bots, 1).valid && p[AssignStandSlot(p, bots, 1).point]->seq == 0);
            CHECK(p[AssignStandSlot(p, bots, 2).point]->seq == 0);
            CHECK(p[AssignStandSlot(p, bots, 3).point]->seq == 1);
        }
        // A zero line direction falls back to straight out from the anchor.
        {
            std::vector<BossStandRow> out = { Row(1, 0, 0, 8, StandRole::Ranged, 777, 0, 0, 0, 32, 0, 0) };
            const std::vector<const BossStandRow*> p = Points(out);
            std::vector<StandBot> bots = { { 1, 2, 10 }, { 2, 2, 10 } };
            StandSlot second = AssignStandSlot(p, bots, 2);
            CHECK(second.valid && second.slot == 1);
            CHECK(std::fabs(second.x) < 0.001f && std::fabs(second.y - (32.0f + kBossStandSpacing)) < 0.001f);
        }
        // A bot on the lane line (a tie): the side with fewer bots; then the lower side.
        {
            std::vector<StandBot> bots = { { 1, 0, 0 }, { 2, 0, 0 }, { 3, 0, 0 } };
            // id order: bot 1 ties, no one seated: side 0. Bot 2: side 0 has one, side 1 none: side 1. Bot 3: tie, 1-1: side 0.
            CHECK(points[AssignStandSlot(points, bots, 1).point]->side == 0);
            CHECK(points[AssignStandSlot(points, bots, 2).point]->side == 1);
            CHECK(points[AssignStandSlot(points, bots, 3).point]->side == 0);
        }
        // Only one side present: that side, whatever the bot's position.
        {
            std::vector<BossStandRow> one = { Row(1, 0, 0, 8, StandRole::Ranged, 777, 1, 0, 0, -32, 1, 0) };
            const std::vector<const BossStandRow*> p = Points(one);
            std::vector<StandBot> bots = { { 1, 2, 10 } };
            CHECK(AssignStandSlot(p, bots, 1).valid);
        }
        // Every bot sees the same plan whatever its own id: the order of the list does not matter.
        {
            std::vector<StandBot> a = { { 5, 2, 10 }, { 3, 2, 10 }, { 9, 2, -10 }, { 1, 2, 10 } };
            std::vector<StandBot> b = { { 1, 2, 10 }, { 9, 2, -10 }, { 3, 2, 10 }, { 5, 2, 10 } };
            for (std::uint64_t id : { 1, 3, 5, 9 })
            {
                StandSlot sa = AssignStandSlot(points, a, id);
                StandSlot sb = AssignStandSlot(points, b, id);
                CHECK(sa.valid && sb.valid && sa.point == sb.point && sa.slot == sb.slot);
            }
        }
    }

    // Arrival: within the tolerance the bot stays put.
    CHECK(StandArrived(0, 0, 0, 0));
    CHECK(StandArrived(0, 0, kBossStandArrived, 0));
    CHECK(!StandArrived(0, 0, kBossStandArrived + 0.5f, 0));
    CHECK(!StandArrived(0, 0, 3, 3));

    std::cout << "All boss stand policy tests passed.\n";
    return 0;
}
