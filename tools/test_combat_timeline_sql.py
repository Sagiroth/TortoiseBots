"""Guards the rows of data/sql/world/20261107110000_world.sql and 20261107210000_world.sql that fill ai_combat_aura_watch.

The combat timeline (AiPlayerbot.CombatTimeline) lists an aura's apply/remove only for the spell ids
in this table (runtime/CombatMonitorService.cpp LoadTimelineWatch). A wrong or missing id silently
drops the aura from every fight record, so this pins the table shape, the idempotent DELETE and the
six Zul'Gurub seeds of the first migration and the two (Hex, Brainwash) of the second.

With TORTOISE_CORE=<tortoise-wow checkout> set, the ids are checked against the core's own data:
each one must be a spell_template row and its name must match the seed's comment.

Run: python3 -m unittest tools/test_combat_timeline_sql.py
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIGRATION = os.path.join(ROOT, "data/sql/world/20261107110000_world.sql")
MIGRATION2 = os.path.join(ROOT, "data/sql/world/20261107210000_world.sql")

# The seeds the ZG investigations need, and the spell_template name each one must carry.
SEEDS = {
    24210: "Mark of Arlokk",
    24306: "Delusions of Jin'do",
    24321: "Poisonous Blood",
    24327: "Cause Insanity",
    24328: "Corrupted Blood",
    24466: "Banish",
}

# The second migration: Jin'do's Hex and Brainwash (the Auras panel's Hex preset reads them).
SEEDS2 = {
    17172: "Hex",
    24261: "Brainwash",
}
# The core spells the second migration's ids are, where its name is spelled differently from our comment's.
CORE_NAMES2 = {24261: "Brain Wash"}

ROW = re.compile(r"^\((\d+), '((?:[^']|'')*)'\)[,;]$")


def migration_text(path=MIGRATION):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def insert_rows(text):
    rows = []
    in_insert = False
    for line in text.splitlines():
        if line.startswith("INSERT INTO `ai_combat_aura_watch`"):
            in_insert = True
            continue
        if not in_insert or line.startswith("--") or not line.strip():
            continue
        match = ROW.match(line)
        if not match:
            raise AssertionError("unparsable ai_combat_aura_watch row: %s" % line)
        rows.append((int(match.group(1)), match.group(2).replace("''", "'")))
        if line.endswith(";"):
            in_insert = False
    return rows


def core_spell_rows(path, spell_id):
    """The spell_template rows of one id: every quoted text of each line that starts with (id, ."""
    rows = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith("(%d, " % spell_id):
                continue
            rows.append([m.replace("''", "'") for m in re.findall(r"'((?:[^']|'')*)'", line)])
    return rows


class CombatTimelineSQL(unittest.TestCase):
    def test_table_shape(self):
        text = migration_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS `ai_combat_aura_watch`", text)
        self.assertRegex(text, r"`spell_id` int unsigned NOT NULL,")
        self.assertRegex(text, r"PRIMARY KEY \(`spell_id`\)")

    def test_idempotent_delete_matches_the_seeds(self):
        text = migration_text()
        deleted = re.search(r"DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN \(([\d, ]+)\)", text)
        self.assertIsNotNone(deleted, "the migration has no scoped DELETE")
        self.assertEqual(sorted(int(x) for x in deleted.group(1).split(",")), sorted(SEEDS),
                         "the DELETE must remove exactly the rows the INSERT writes, so other rows survive")

    def test_seeds(self):
        rows = insert_rows(migration_text())
        self.assertEqual(sorted(row[0] for row in rows), sorted(SEEDS))
        self.assertEqual(len(rows), len(SEEDS), "a seed row is duplicated")
        for spell_id, comment in rows:
            self.assertIn(SEEDS[spell_id], comment,
                          "spell %d's comment does not name %r" % (spell_id, SEEDS[spell_id]))

    def test_second_migration_shape_delete_and_seeds(self):
        text = migration_text(MIGRATION2)
        self.assertIn("CREATE TABLE IF NOT EXISTS `ai_combat_aura_watch`", text)
        deleted = re.search(r"DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN \(([\d, ]+)\)", text)
        self.assertIsNotNone(deleted, "the second migration has no scoped DELETE")
        self.assertEqual(sorted(int(x) for x in deleted.group(1).split(",")), sorted(SEEDS2),
                         "the DELETE must remove exactly the rows the INSERT writes")
        rows = insert_rows(text)
        self.assertEqual(sorted(row[0] for row in rows), sorted(SEEDS2))
        self.assertEqual(len(rows), len(SEEDS2), "a seed row is duplicated")
        for spell_id, comment in rows:
            self.assertIn(SEEDS2[spell_id], comment,
                          "spell %d's comment does not name %r" % (spell_id, SEEDS2[spell_id]))
        self.assertFalse(set(SEEDS) & set(SEEDS2), "the two migrations seed different spells")


CORE = os.environ.get("TORTOISE_CORE")
CORE_SPELLS = os.path.join(CORE or "", "sql", "base", "tw_world_spell_template.sql")


@unittest.skipUnless(CORE and os.path.isfile(CORE_SPELLS), "set TORTOISE_CORE=<tortoise-wow checkout>")
class CombatTimelineCoreData(unittest.TestCase):
    def test_every_seed_is_the_expected_core_spell(self):
        for spell_id, name in SEEDS.items():
            rows = core_spell_rows(CORE_SPELLS, spell_id)
            self.assertTrue(rows, "spell %d has no spell_template row" % spell_id)
            self.assertTrue(any(name in texts for texts in rows),
                            "spell %d is not %r in spell_template" % (spell_id, name))

    def test_every_second_migration_seed_is_the_expected_core_spell(self):
        for spell_id, name in SEEDS2.items():
            name = CORE_NAMES2.get(spell_id, name)
            rows = core_spell_rows(CORE_SPELLS, spell_id)
            self.assertTrue(rows, "spell %d has no spell_template row" % spell_id)
            self.assertTrue(any(name in texts for texts in rows),
                            "spell %d is not %r in spell_template" % (spell_id, name))


if __name__ == "__main__":
    unittest.main()
