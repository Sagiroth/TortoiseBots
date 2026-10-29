"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_stand.

The loader (ai/playerbot/BossStandMgr.cpp, LoadBossStands) skips a bad row with only a
count in the log, so a typo in a migration would silently turn a stand set off. This reads
every INSERT into ai_boss_stand and checks what BossStandTable::Add checks, plus the
primary key and, for Onyxia's rows (Onyxia kill plan, item C3), the geometry the plan
rests on: a point is only safe from Deep Breath when every slot of it is 30 yd or more
from every link of the breath lane.

Run: python3 -m unittest tools/test_boss_stand_sql.py
"""
import glob
import math
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROLES = {"ranged", "healer", "melee"}
MAX_SLOTS = 8
SPACING = 9.0
ROW = re.compile(
    r"^\((\d+), (\d+), (\d+), (-?[\d.]+), (-?[\d.]+), ([\d.]+), '(\w+)', (\d+), (\d+), (\d+), (\d+), "
    r"(-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (\d+), '(?:[^']|'')*'\)[,;]$"
)
FIELDS = ("map", "boss", "anchor", "ax", "ay", "ar", "role", "window", "aura", "side", "seq",
          "x", "y", "z", "dx", "dy", "slots")

ONYXIA_MAP = 249
ONYXIA = 10184
HOVER_AURA = 17131
# The 8 hover points (core boss_onyxia.cpp aMoveData: x, y) in order of the core's point id, each with
# the breath spell the boss casts AT that point and the id of the opposite point (the lane's far end).
HOVER = [
    (10.2191, -247.912, 18617),    # 0 NE, NE to SW
    (-31.4963, -250.123, 18576),   # 1 E, E to W
    (-63.5156, -240.096, 18564),   # 2 SE, SE to NW
    (-65.8444, -213.809, 18351),   # 3 S, S to N
    (-58.2509, -189.020, 18596),   # 4 SW, SW to NE
    (-33.5561, -182.682, 18609),   # 5 W, W to E
    (6.8951, -180.246, 18584),     # 6 NW, NW to SE
    (22.8763, -217.152, 17086),    # 7 N, N to S (the 20-link lane runs on into the tunnel to -130.8 -213.4)
]
TUNNEL_END = (-130.8, -213.4)
# Clearance from every link of the lane: a link hits everything within 30 yd of its point.
BREATH_RADIUS = 30.0
# The egg fields (core: eggs 176511): x -115..-32 with y < -240 east or y > -180 west. A bot that steps on
# the field hatches whelps, so no slot stands in it (with a margin).
EGG_X = (-119.0, -29.0)
EGG_Y_EAST = -237.0
EGG_Y_WEST = -183.0


def migration_rows():
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "INSERT INTO `ai_boss_stand`" not in text:
            continue
        in_insert = False
        for line in text.splitlines():
            if line.startswith("INSERT INTO `ai_boss_stand`"):
                in_insert = True
                continue
            if not in_insert or line.startswith("--") or not line.strip():
                continue
            match = ROW.match(line)
            if not match:
                raise AssertionError("unparsable ai_boss_stand row in %s: %s" % (os.path.basename(path), line))
            values = match.groups()
            row = {"file": os.path.basename(path)}
            for name, value in zip(FIELDS, values):
                row[name] = value if name == "role" else float(value)
            rows.append(row)
            if line.endswith(";"):
                in_insert = False
    return rows


def slot_positions(row):
    length = math.hypot(row["dx"], row["dy"])
    if length > 0.0001:
        ux, uy = row["dx"] / length, row["dy"] / length
    else:
        ux, uy = row["x"] - row["ax"], row["y"] - row["ay"]
        length = math.hypot(ux, uy)
        ux, uy = ux / length, uy / length
    return [(row["x"] + ux * SPACING * k, row["y"] + uy * SPACING * k) for k in range(int(row["slots"]))]


def segment_distance(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (a[0] + t * dx), py - (a[1] + t * dy))


def onyxia_rows():
    return [r for r in migration_rows() if r["map"] == ONYXIA_MAP and r["boss"] == ONYXIA]


class BossStandSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_stand rows found")

    def test_rows_are_loadable(self):
        for row in migration_rows():
            where = "%s boss %d anchor %d" % (row["file"], row["boss"], row["anchor"])
            self.assertIn(row["role"], ROLES, where)
            self.assertGreater(row["map"], 0, where)
            self.assertGreater(row["boss"], 0, where)
            self.assertGreater(row["ar"], 0.0, where)
            self.assertIn(int(row["side"]), (0, 1), where)
            self.assertGreaterEqual(row["slots"], 1, where)
            self.assertLessEqual(row["slots"], MAX_SLOTS, where)
            self.assertLess(row["seq"], 256, where)

    def test_primary_keys_are_unique(self):
        seen = set()
        for row in migration_rows():
            key = (row["map"], row["boss"], row["anchor"], row["role"], row["window"], row["side"], row["seq"])
            self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, row["file"]))
            seen.add(key)

    def test_one_anchor_id_is_one_place(self):
        # BossStandTable::Add refuses a row whose anchor id disagrees with the first row of that id.
        anchors = {}
        for row in migration_rows():
            key = (row["map"], row["boss"], row["anchor"])
            place = (row["ax"], row["ay"], row["ar"])
            self.assertEqual(anchors.setdefault(key, place), place, "anchor %s names two places" % (key,))

    def test_onyxia_has_the_eight_hover_anchors_with_their_breath(self):
        rows = onyxia_rows()
        self.assertTrue(rows, "no Onyxia stand rows")
        for anchor, (x, y, spell) in enumerate(HOVER):
            mine = [r for r in rows if r["anchor"] == anchor]
            self.assertTrue(mine, "hover point %d has no stand rows" % anchor)
            for row in mine:
                self.assertAlmostEqual(row["ax"], x, delta=0.1, msg="anchor %d x" % anchor)
                self.assertAlmostEqual(row["ay"], y, delta=0.1, msg="anchor %d y" % anchor)
                self.assertEqual(row["ar"], 8.0, "anchor %d radius" % anchor)
                # Only while she casts the breath of her current point, in the air.
                self.assertEqual(row["window"], spell, "anchor %d window spell" % anchor)
                self.assertEqual(row["aura"], HOVER_AURA, "anchor %d aura" % anchor)

    def test_onyxia_window_spells_are_the_breath_ids_of_the_dodge_policy(self):
        # The reactive dodge (policy/raid/OnyxiaBreathPolicy.h) lists the same eight ids.
        with open(os.path.join(ROOT, "ai/playerbot/policy/raid/OnyxiaBreathPolicy.h"), encoding="utf-8") as handle:
            listed = set(int(n) for n in re.findall(r"case (\d{5}):\s*//", handle.read()))
        self.assertEqual(listed, set(spell for _, _, spell in HOVER))
        self.assertEqual(set(int(r["window"]) for r in onyxia_rows()), listed)

    def test_onyxia_rows_are_for_ranged_and_healers_only(self):
        # Melee stays on her when she lands; no tank or melee row.
        self.assertEqual(set(r["role"] for r in onyxia_rows()), {"ranged", "healer"})

    def test_onyxia_every_anchor_seats_both_roles_on_both_sides(self):
        for anchor in range(len(HOVER)):
            for role in ("ranged", "healer"):
                sides = set(int(r["side"]) for r in onyxia_rows() if r["anchor"] == anchor and r["role"] == role)
                self.assertTrue(sides, "anchor %d has no %s rows" % (anchor, role))

    def test_onyxia_every_slot_is_clear_of_the_breath_lane(self):
        for row in onyxia_rows():
            anchor = int(row["anchor"])
            start = HOVER[anchor][:2]
            end = HOVER[(anchor + 4) % 8][:2]
            if anchor == 7:
                end = TUNNEL_END  # N to S runs on into the tunnel
            for x, y in slot_positions(row):
                distance = segment_distance(x, y, start, end)
                self.assertGreaterEqual(
                    distance, BREATH_RADIUS + 0.9,
                    "anchor %d %s side %d seq %d slot at (%.1f, %.1f) is %.1f yd from the lane" % (
                        anchor, row["role"], row["side"], row["seq"], x, y, distance))

    def test_onyxia_no_slot_stands_in_an_egg_field(self):
        for row in onyxia_rows():
            for x, y in slot_positions(row):
                inside = EGG_X[0] <= x <= EGG_X[1] and (y < EGG_Y_EAST or y > EGG_Y_WEST)
                self.assertFalse(inside, "slot (%.1f, %.1f) of anchor %d is in an egg field" % (x, y, row["anchor"]))

    def test_onyxia_slots_of_one_anchor_are_spread(self):
        # Fireball 18392 splashes 8 yd: no two seats of the anchor closer than 9 yd, across roles and sides.
        for anchor in range(len(HOVER)):
            seats = []
            for row in onyxia_rows():
                if row["anchor"] == anchor:
                    seats.extend(slot_positions(row))
            for i in range(len(seats)):
                for j in range(i + 1, len(seats)):
                    distance = math.hypot(seats[i][0] - seats[j][0], seats[i][1] - seats[j][1])
                    self.assertGreaterEqual(distance, SPACING - 0.2,
                                            "anchor %d seats %s and %s are %.1f yd apart" % (anchor, seats[i], seats[j], distance))

    def test_onyxia_lane_edge_points_hold_the_whole_raid(self):
        # 17 ranged and 10 healers in the 40-man roster; what does not fit keeps the reactive dodge.
        # The N and S anchors sit on the narrow axis of the room (the shallow bands y < -246 and y > -184).
        for anchor in range(len(HOVER)):
            ranged = sum(int(r["slots"]) for r in onyxia_rows() if r["anchor"] == anchor and r["role"] == "ranged")
            healers = sum(int(r["slots"]) for r in onyxia_rows() if r["anchor"] == anchor and r["role"] == "healer")
            self.assertGreaterEqual(ranged, 12, "anchor %d seats %d ranged" % (anchor, ranged))
            self.assertGreaterEqual(healers, 2, "anchor %d seats %d healers" % (anchor, healers))


if __name__ == "__main__":
    unittest.main()
