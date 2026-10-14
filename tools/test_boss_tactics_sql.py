"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_tactic.

The loader (ai/playerbot/BossTacticsMgr.cpp) skips a bad row with only a count in the
log, so a typo in a migration would silently turn a tactic off. This reads every
INSERT into ai_boss_tactic and checks what BossTacticTable::Add checks, plus the
primary key and the shape of each kind.
"""
import glob
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = {"interrupt", "kill_first", "behind"}
ROW = re.compile(r"^\((\d+), (\d+), '(\w+)', (\d+), (\d+), (\d+), (-?[\d.]+), '[^']*'\)[,;]$")


def migration_rows():
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "INSERT INTO `ai_boss_tactic`" not in text:
            continue
        in_insert = False
        for line in text.splitlines():
            if line.startswith("INSERT INTO `ai_boss_tactic`"):
                in_insert = True
                continue
            if not in_insert or line.startswith("--") or not line.strip():
                continue
            match = ROW.match(line)
            if not match:
                raise AssertionError("unparsable ai_boss_tactic row in %s: %s" % (os.path.basename(path), line))
            rows.append((os.path.basename(path),) + match.groups())
            if line.endswith(";"):
                in_insert = False
    return rows


class BossTacticsSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_tactic rows found")

    def test_rows_are_loadable(self):
        for name, map_id, boss, kind, target, spell, priority, param in migration_rows():
            where = "%s boss %s kind %s" % (name, boss, kind)
            self.assertIn(kind, KINDS, where)
            self.assertGreater(int(map_id), 0, where)
            self.assertGreater(int(boss), 0, where)
            self.assertLess(int(priority), 256, where)
            if kind == "interrupt":
                self.assertGreater(int(spell), 0, where)
            if kind == "behind":
                self.assertGreater(float(param), 0.0, where)

    def test_primary_keys_are_unique(self):
        seen = set()
        for name, map_id, boss, kind, target, spell, _priority, _param in migration_rows():
            key = (map_id, boss, kind, target, spell)
            self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, name))
            seen.add(key)


if __name__ == "__main__":
    unittest.main()
