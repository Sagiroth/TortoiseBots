"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_stand.

The loader (ai/playerbot/BossStandMgr.cpp, LoadBossStands) skips a bad row with only a
count in the log, so a typo in a migration would silently turn a stand set off. This reads
every INSERT into ai_boss_stand and checks what BossStandTable::Add checks, plus the
primary key and, for each boss with rows, the geometry its seats rest on.

Run: python3 -m unittest tools/test_boss_stand_sql.py
"""
import collections
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
    r"(-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (\d+), (\d+), (\d+), '(\w+)', '(?:[^']|'')*'\)[,;]$"
)
FIELDS = ("map", "boss", "anchor", "ax", "ay", "ar", "role", "window", "aura", "side", "seq",
          "x", "y", "z", "dx", "dy", "slots", "hold", "release", "signal")

VENOXIS_MAP = 309
VENOXIS = 14507
# The boss's own fight spot in the walk-22 fights: while he fights there the frames put 14507 within 5.01 yd of
# this anchor (frames.jsonl, hp < 100 %: -12015.8 -1690.5 the kill, -12020.6 -1695.1 r6, -12019.1 -1696.7 test2).
# The radius is that drift plus 2 yd of margin; the spawn -12029.8 -1707.9, 17.5 yd out, is only ever at 100 %.
VENOXIS_ANCHOR = (-12018.0, -1695.0, 7.0)
# The stand is up while the boss is anywhere in that disk, so a seat has to keep a caster's range wherever he is
# inside it: the nearest he may come is 15 yd (Holy Nova 23858 reaches that far), the farthest 30 yd (a caster's
# range). With the 7 yd radius that is a seat distance of 22-23 yd from the anchor.
VENOXIS_SEAT_MIN_BOSS = 15.0
VENOXIS_SEAT_MAX_BOSS = 30.0
# Holy Wrath 23979 links the unit nearest its last one within 10 yd plus bounding radii: two bots 11 yd apart
# cannot be linked, so the chain cannot walk out of the melee clump.
VENOXIS_SEAT_GAP = 11.0
# The core evades Venoxis outside z 27-43 (boss_venoxis.cpp).
VENOXIS_SEAT_Z = (27.0, 43.0)
VENOXIS_SEATS = 12


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
                row[name] = value if name in ("role", "signal") else float(value)
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


def venoxis_rows():
    return [r for r in migration_rows() if r["map"] == VENOXIS_MAP and r["boss"] == VENOXIS]


class BossStandSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_stand rows found")

    def test_rows_are_loadable(self):
        for row in migration_rows():
            where = "%s boss %d anchor %d" % (row["file"], row["boss"], row["anchor"])
            self.assertIn(row["role"], ROLES, where)
            self.assertIn(row["signal"], ("cast", "stopped_no_target"), where)
            if row["signal"] == "stopped_no_target":
                # BossStandTable::Add: a signal that opens the window needs an aura and a window spell.
                self.assertGreater(row["aura"], 0, where)
                self.assertGreater(row["window"], 0, where)
            self.assertGreater(row["map"], 0, where)
            self.assertGreater(row["boss"], 0, where)
            self.assertGreater(row["ar"], 0.0, where)
            self.assertIn(int(row["side"]), (0, 1), where)
            self.assertGreaterEqual(row["slots"], 1, where)
            self.assertLessEqual(row["slots"], MAX_SLOTS, where)
            self.assertLess(row["seq"], 256, where)
            if row["release"]:
                self.assertNotEqual(row["release"], row["anchor"], where)
            # BossStandTable::Add bounds the hold: a longer one parks bots for as long as it lasts.
            self.assertGreaterEqual(row["hold"], 1000, where)
            self.assertLessEqual(row["hold"], 15000, where)

    def test_release_anchors_name_an_anchor_of_the_same_boss(self):
        # BossStandTable::Seal refuses a boss's whole stand set when one does not.
        anchors = {}
        for row in migration_rows():
            anchors.setdefault((row["map"], row["boss"]), set()).add(row["anchor"])
        for row in migration_rows():
            if row["release"]:
                self.assertIn(row["release"], anchors[(row["map"], row["boss"])],
                              "%s anchor %d names release anchor %d, which the boss does not hold" % (
                                  row["file"], row["anchor"], row["release"]))

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

    def test_the_table_migration_creates_the_table_only(self):
        # The table comes empty; each boss's rows come in that boss's own migration.
        path = os.path.join(ROOT, "data/sql/world/20261101075500_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("CREATE TABLE IF NOT EXISTS `ai_boss_stand`", text)
        self.assertNotIn("INSERT", text)
        self.assertNotIn("DELETE", text)


class BossStandVenoxisSql(unittest.TestCase):
    def test_venoxis_seats_hold_their_range_anywhere_the_boss_may_stand(self):
        # walk 22: the raid stood 1-16 yd from the boss (r6: all 19 inside 16 yd, the healers 3.5-8.7 yd) and one
        # Holy Wrath 23979 chain killed 9 bots inside a second (hits 5.4-57 k, k11-k21). The seats hold the ranged
        # and the healers out of the melee clump and 11 yd apart, checked against the whole disk the boss may be in
        # while the stand is up (the anchor +- the radius), not the anchor point: over that disk the nearest the
        # boss may come to a seat is 15 yd and the farthest 30 yd.
        rows = venoxis_rows()
        self.assertTrue(rows, "no Venoxis stand rows")
        self.assertEqual(1, len(set((r["ax"], r["ay"], r["ar"]) for r in rows)),
                         "the seats name more than one anchor")
        for row in rows:
            self.assertAlmostEqual(VENOXIS_ANCHOR[0], row["ax"], delta=0.1)
            self.assertAlmostEqual(VENOXIS_ANCHOR[1], row["ay"], delta=0.1)
            self.assertAlmostEqual(VENOXIS_ANCHOR[2], row["ar"], delta=0.1)
        seats = []
        for row in rows:
            where = "%s anchor %d %s seq %d" % (row["file"], row["anchor"], row["role"], row["seq"])
            # One bot a row: kBossStandSpacing is 9 yd, under the chain's 10 yd jump.
            self.assertEqual(1.0, row["slots"], "%s seats %s bots" % (where, row["slots"]))
            self.assertIn(row["role"], ("ranged", "healer"), where)
            # No window and no aura: the seats are up for the whole P1 fight, not only around a cast.
            self.assertEqual(0.0, row["window"], where)
            self.assertEqual(0.0, row["aura"], where)
            self.assertEqual("cast", row["signal"], where)
            self.assertGreaterEqual(row["z"], VENOXIS_SEAT_Z[0], where)
            self.assertLessEqual(row["z"], VENOXIS_SEAT_Z[1], where)
            for x, y in slot_positions(row):
                distance = math.hypot(x - VENOXIS_ANCHOR[0], y - VENOXIS_ANCHOR[1])
                # Worst case over the whole disk: the boss at the near edge is distance - radius from the seat, at
                # the far edge distance + radius. Both use the row's own radius, so widening it turns this red.
                self.assertGreaterEqual(distance - row["ar"], VENOXIS_SEAT_MIN_BOSS - 0.05,
                                        "%s: seat (%.2f, %.2f) is only %.2f yd from the boss at the near edge" % (
                                            where, x, y, distance - row["ar"]))
                self.assertLessEqual(distance + row["ar"], VENOXIS_SEAT_MAX_BOSS + 0.05,
                                     "%s: seat (%.2f, %.2f) is %.2f yd from the boss at the far edge" % (
                                         where, x, y, distance + row["ar"]))
                seats.append((x, y, where))
        self.assertGreaterEqual(len(seats), VENOXIS_SEATS, "only %d seats" % len(seats))
        roles = collections.Counter(row["role"] for row in rows)
        self.assertGreaterEqual(roles["ranged"], 1, "no ranged seats: %s" % dict(roles))
        self.assertGreaterEqual(roles["healer"], 1, "no healer seats: %s" % dict(roles))
        for i in range(len(seats)):
            for j in range(i + 1, len(seats)):
                distance = math.hypot(seats[i][0] - seats[j][0], seats[i][1] - seats[j][1])
                self.assertGreaterEqual(distance, VENOXIS_SEAT_GAP - 0.05,
                                        "%s and %s are %.2f yd apart" % (seats[i][2], seats[j][2], distance))


if __name__ == "__main__":
    unittest.main()
