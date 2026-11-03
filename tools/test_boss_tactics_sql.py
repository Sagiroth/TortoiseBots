"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_tactic.

The loader (ai/playerbot/BossTacticsMgr.cpp) skips a bad row with only a count in the
log, so a typo in a migration would silently turn a tactic off. This reads every
INSERT into ai_boss_tactic, replays the later DELETEs to keep only the rows that are
live, and checks what BossTacticTable::Add checks, plus the primary key and the shape
of each kind.

With TORTOISE_CORE=<tortoise-wow checkout> set, BossTacticsCoreData also checks every
live interrupt row against the core's own data: the spell exists, the core's
EffectInterruptCast rule can interrupt it, the boss and target templates exist, the
boss spawns on its map (or is summoned), and no core update changes a column the rule
reads.
"""
import glob
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = {"interrupt", "kill_first", "behind", "avoid_cast", "tank_hold", "hold_kill", "one_at_a_time",
         "linked_death", "dps_hold_after", "dispel_priority", "flee_on_aura", "freeze_on_aura",
         "taunt_after_cast", "swap_on_aura"}
# Kinds read straight from the table (the dps and tank pickers, the linked_death scan), never from a fight scan: a row
# of one of them cannot answer a gate, so the loader refuses one.
TABLE_READ_KINDS = {"hold_kill", "tank_hold", "one_at_a_time", "linked_death"}
# The mode word each kind takes (`tactic_mode`); an empty word is the kind's own behaviour.
MODE_WORDS = {
    "dispel_priority": {"now", "clear", "never"},
    "flee_on_aura": {"away", "to_tank"},
    "dps_hold_after": {"engage", "blink", "until_tank"},
    "taunt_after_cast": {"engage", "blink", "until_tank"},
}
# A row as the replay hands it around: (file,) + the table columns in this order.
FIELDS = ("file", "map", "boss_entry", "kind", "target_entry", "spell_id", "priority", "param", "burn_order",
          "hp_min", "hp_max", "bot_aura", "tactic_mode", "window_s")
INSERT_COLUMNS = {
    "map": 0, "boss_entry": 0, "kind": "", "target_entry": 0, "spell_id": 0, "priority": 0, "param": 0.0,
    "burn_order": None, "comment": "", "hp_min": 0, "hp_max": 100, "bot_aura": 0, "tactic_mode": "",
    "window_s": 7.0,
}
DELETE = re.compile(
    r"^DELETE FROM `ai_boss_tactic` WHERE `map` = (\d+)"
    r"(?: AND `boss_entry` IN \(([\d, ]+)\)| AND `boss_entry` = (\d+))?"
    r"(?: AND `kind` = '(\w+)')?"
    r"(?: AND `target_entry` = (\d+) AND `spell_id` = (\d+))?;?$"
)
DELETE_LINE = re.compile(r"DELETE\s+FROM\s+`?ai_boss_tactic`?(?=\s|;|$)", re.IGNORECASE)
INSERT = re.compile(
    r"^(?:INSERT|REPLACE)\s+INTO\s+`ai_boss_tactic`\s*\(([^)]*)\)\s*VALUES\s*(.+)$", re.IGNORECASE | re.S
)
UPDATE = re.compile(r"^UPDATE\s+`ai_boss_tactic`\s+SET\s+(.+?)\s+WHERE\s+(.+)$", re.IGNORECASE | re.S)


def migration_paths():
    return sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql")))


def _row_dict(name, columns, tuple_text):
    """One VALUES tuple as {column: value}, the absent columns at their table default."""
    values = _selected_values(tuple_text, set(range(len(columns))))
    if len(values) != len(columns):
        raise AssertionError("unparsable ai_boss_tactic row in %s: (%s)" % (name, tuple_text.strip()))
    row = dict(INSERT_COLUMNS)
    for column, index in zip(columns, range(len(columns))):
        if column not in INSERT_COLUMNS:
            raise AssertionError("unknown ai_boss_tactic column `%s` in %s" % (column, name))
        row[column] = _value(values[index])
    if row["map"] is None or row["boss_entry"] is None or not row["kind"]:
        raise AssertionError("ai_boss_tactic row without map/boss/kind in %s: (%s)" % (name, tuple_text.strip()))
    row["map"] = int(row["map"])
    row["boss_entry"] = int(row["boss_entry"])
    row["target_entry"] = int(row["target_entry"] or 0)
    row["spell_id"] = int(row["spell_id"] or 0)
    row["priority"] = int(row["priority"] or 0)
    row["param"] = float(row["param"] or 0.0)
    row["hp_min"] = int(row["hp_min"] or 0)
    row["hp_max"] = 100 if row["hp_max"] is None else int(row["hp_max"])
    row["bot_aura"] = int(row["bot_aura"] or 0)
    row["tactic_mode"] = row["tactic_mode"] or ""
    row["window_s"] = 7.0 if row["window_s"] is None else float(row["window_s"])
    return row


def _payload(row, name):
    row = dict(row)
    row["file"] = name
    return tuple(row[field] for field in FIELDS)


def _payload_dict(payload):
    return dict(zip(FIELDS, payload))


def migration_statements():
    """Yield ("insert", payload) and ("delete", (name, match)) in file and statement order."""
    for path in migration_paths():
        name = os.path.basename(path)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_boss_tactic`" not in text:
            continue
        for statement in _sql_statements(text):
            line = statement.strip()
            match = INSERT.match(line)
            if match:
                columns = [column.strip().strip("`") for column in match.group(1).split(",")]
                for tuple_text in _tuples(match.group(2)):
                    yield "insert", _payload(_row_dict(name, columns, tuple_text), name)
                continue
            if DELETE_LINE.match(line):
                match = DELETE.match(line)
                if not match:
                    raise AssertionError("unparsable DELETE in %s: %s" % (name, line))
                yield "delete", (name, match)


def migration_rows():
    return [row for kind, row in migration_statements() if kind == "insert"]


def migration_updates():
    """The scoped `UPDATE ai_boss_tactic SET ... WHERE ...` statements the replay applies, in file order."""
    updates = []
    for path in migration_paths():
        name = os.path.basename(path)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_boss_tactic`" not in text:
            continue
        for statement in _sql_statements(text):
            match = UPDATE.match(statement.strip())
            if not match:
                continue
            changes = {}
            for part in re.split(r"\s*,\s*", match.group(1).strip()):
                found = re.fullmatch(r"`(\w+)`\s*=\s*(.+)", part, re.S)
                if not found:
                    raise AssertionError("unparsable SET in %s: %s" % (name, part))
                column, value = found.groups()
                if column not in INSERT_COLUMNS:
                    continue  # a column the replay does not track
                changes[column] = _value(value)
            updates.append((name, changes, _conditions(match.group(2), name)))
    return updates


def _conditions(text, name):
    """{column: value or set of values} of a WHERE clause over the row columns the replay tracks."""
    wanted = {}
    for part in re.split(r"\s+AND\s+", text.strip(), re.I):
        found = re.fullmatch(r"`(\w+)`\s*=\s*(.+)", part.strip(), re.S)
        if found:
            column, raw = found.groups()
            if column not in INSERT_COLUMNS:
                continue
            wanted[column] = _value(raw)
            continue
        found = re.fullmatch(r"`(\w+)`\s+IN\s*\((.+)\)", part.strip(), re.I | re.S)
        if found:
            column, raw = found.groups()
            if column not in INSERT_COLUMNS:
                continue
            wanted[column] = {_value(item) for item in raw.split(",")}
            continue
        raise AssertionError("unsupported WHERE clause in %s: %s" % (name, part.strip()))
    return wanted


def _matches(row, wanted):
    for column, value in wanted.items():
        if isinstance(value, set):
            if row[column] not in value:
                return False
        elif row[column] != value:
            return False
    return True


def delete_scope(match):
    """(map, bosses, single entry, kind, target, spell) of a parsed DELETE, each None when absent."""
    return (
        int(match.group(1)),
        {int(entry) for entry in match.group(2).split(",")} if match.group(2) else None,
        int(match.group(3)) if match.group(3) else None,
        match.group(4),
        int(match.group(5)) if match.group(5) else None,
        int(match.group(6)) if match.group(6) else None,
    )


def delete_covers(scope, key):
    """Whether the parsed DELETE removes one (map, boss_entry, kind, target_entry, spell_id) key."""
    map_id, bosses, entry, kind, target, spell = scope
    if key[0] != map_id:
        return False
    if entry is not None and key[1] != entry:
        return False
    if bosses is not None and key[1] not in bosses:
        return False
    if kind is not None and key[2] != kind:
        return False
    if target is not None and key[3] != target:
        return False
    if spell is not None and key[4] != spell:
        return False
    return True


def effective_rows():
    """The rows a boot ends up with: every INSERT, minus the DELETEs that follow it, plus the scoped UPDATEs."""
    live = {}
    for kind, payload in migration_statements():
        if kind == "insert":
            # The key includes the health band, so one boss carries the same kind once per band.
            live[(payload[1], payload[2], payload[3], payload[4], payload[5], payload[9], payload[10])] = payload
            continue
        _name, match = payload
        scope = delete_scope(match)
        for key in [key for key in live if delete_covers(scope, key[:5])]:
            del live[key]
    rows = list(live.values())
    for _name, changes, wanted in migration_updates():
        for index, payload in enumerate(rows):
            row = _payload_dict(payload)
            if _matches(row, wanted):
                row.update(changes)
                rows[index] = _payload(row, payload[0])
    return rows


class BossTacticsSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_tactic rows found")

    def test_rows_are_loadable(self):
        for row in effective_rows():
            name, map_id, boss, kind, target, spell, priority, param, burn_order = row[:9]
            hp_min, hp_max, bot_aura, mode, window_s = row[9:]
            where = "%s boss %s kind %s" % (name, boss, kind)
            self.assertIn(kind, KINDS, where)
            self.assertGreater(int(map_id), 0, where)
            self.assertGreater(int(boss), 0, where)
            self.assertLess(int(priority), 256, where)
            # The shared gates: a percentage band and a bot aura. A gate only a fight scan can answer.
            self.assertGreaterEqual(hp_min, 0, where)
            self.assertLessEqual(hp_max, 100, where)
            self.assertLessEqual(hp_min, hp_max, where)
            if hp_min != 0 or hp_max != 100 or bot_aura:
                self.assertNotIn(kind, TABLE_READ_KINDS,
                                 "%s: a gate on a kind read straight from the table" % where)
            # The mode word is the kind's, or empty for its own behaviour.
            self.assertIn(mode, MODE_WORDS.get(kind, set()) | {""}, where)
            if kind == "interrupt":
                self.assertGreater(int(spell), 0, where)
            if kind == "behind":
                self.assertGreater(float(param), 0.0, where)
            if kind == "avoid_cast":
                self.assertGreater(int(spell), 0, where)
                self.assertGreater(float(param), 0.0, where)
            if kind == "one_at_a_time":
                self.assertEqual(int(target), 0, where)
                self.assertEqual(int(spell), 0, where)
            if kind == "linked_death":
                self.assertGreater(int(target), 0, where)     # a partner: a row without one lists nobody
                self.assertGreater(float(param), 0.0, where)  # the hold percent
                self.assertLessEqual(float(param), 100.0, where)
                self.assertGreater(float(window_s), 0.0, where)
                # The burn order is a comma list of creature entries (empty: the ordinary released pick).
                if burn_order:
                    for token in burn_order.split(","):
                        self.assertRegex(token.strip(), r"^\d+$", where)
            if kind == "dps_hold_after":
                self.assertGreater(float(param), 0.0, where)   # the hold in seconds
                self.assertLessEqual(float(param), 30.0, where)
            if kind == "dispel_priority":
                self.assertGreater(int(spell), 0, where)        # the aura to rank
            if kind in ("flee_on_aura", "freeze_on_aura"):
                self.assertGreater(int(spell), 0, where)        # the aura that sends the carrier out
                self.assertGreaterEqual(float(param), 0.0, where)
            if kind == "taunt_after_cast":
                self.assertGreater(float(param), 0.0, where)    # the deadline in seconds
                self.assertTrue(int(spell) > 0 or mode in ("blink", "until_tank"), where)
            if kind == "swap_on_aura":
                self.assertGreater(int(spell), 0, where)        # the aura the tanks swap on
                self.assertGreaterEqual(int(priority), 2, where)  # at least two tanks in the rotation

    def test_primary_keys_are_unique(self):
        # In boot order: an INSERT may not repeat a key that is still live, and a DELETE frees the keys it removes
        # (20261101040000 re-inserts the Mad Voidwalker's avoid_cast row after its boss-scoped DELETE, the shape
        # test_dungeon_plan_sql.py requires when a later migration adds a row to an existing (map, boss_entry)).
        seen = set()
        for kind, payload in migration_statements():
            if kind == "insert":
                name, map_id, boss, row_kind, target, spell = payload[:6]
                key = (int(map_id), int(boss), row_kind, int(target), int(spell), payload[9], payload[10])
                self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, name))
                seen.add(key)
                continue
            _name, match = payload
            scope = delete_scope(match)
            seen = {key for key in seen if not delete_covers(scope, key[:5])}

    def test_deletes_are_map_scoped(self):
        for path in migration_paths():
            with open(path, encoding="utf-8") as handle:
                for line in handle:
                    if not DELETE_LINE.match(line):
                        continue
                    self.assertRegex(
                        line,
                        r"^DELETE\s+FROM\s+`?ai_boss_tactic`?\s+WHERE\s+`map` = \d+",
                        "DELETE not scoped by map in %s: %s" % (os.path.basename(path), line.strip()),
                    )

    def test_hold_kill_paces_the_mad_servants(self):
        # walk 21: the Mad Servant 15111's two hold_kill rows (budget 0: no DPS while a Mad Voidwalker 15146 or a
        # Portal of Madness 15141 lives) pace the kills, so the pair of Voidwalkers a dead Servant leaves spawns one
        # pair at a time instead of eight at once.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5]), int(row[6]))
                for row in effective_rows()}
        self.assertIn((309, 15111, "interrupt", 0, 24612, 0), live, "the Servant's Flamestrike row is gone")
        self.assertIn((309, 15111, "hold_kill", 15146, 0, 0), live)
        self.assertIn((309, 15111, "hold_kill", 15141, 0, 0), live)

        # The migration that writes them DELETEs the (309, 15111) key first and re-inserts the interrupt row from
        # 20261101020000 unchanged (its shape is what test_dungeon_plan_sql.py requires of a re-INSERTed key).
        path = os.path.join(ROOT, "data/sql/world/20261101050000_world.sql")
        self.assertTrue(os.path.isfile(path), "the hold_kill migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        delete = text.index("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15111);")
        self.assertLess(delete, text.index("INSERT INTO `ai_boss_tactic`"))
        with open(os.path.join(ROOT, "data/sql/world/20261101020000_world.sql"), encoding="utf-8") as handle:
            earlier = handle.read()
        interrupt = [line for line in earlier.splitlines() if line.startswith("(309, 15111,")]
        self.assertEqual(len(interrupt), 1)
        # The row's content goes back byte-identical; it is no longer the statement's last row, so its terminator is
        # the comma the two hold_kill rows follow.
        self.assertIn(interrupt[0].rstrip(",;") + ",", text,
                      "the Servant's interrupt row did not go back in unchanged")

    def test_deleted_rows_are_gone(self):
        # pjerra/turtle-agent#246: the rows below are not interruptible under the
        # core's rule (preventionType 0, or interruptFlags without the damage bit)
        # and were deleted by a later migration.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        self.assertNotIn((822, 63133, "interrupt", 0, 3143), live)
        self.assertNotIn((820, 62782, "interrupt", 62771, 36732), live)
        self.assertNotIn((70, 4854, "interrupt", 0, 8292), live)
        self.assertNotIn((309, 14507, "interrupt", 0, 23895), live)
        self.assertNotIn((309, 14834, "interrupt", 0, 24324), live)
        self.assertNotIn((409, 12118, "interrupt", 12119, 20604), live)
        self.assertNotIn((531, 15275, "interrupt", 0, 7393), live)
        self.assertNotIn((531, 15276, "interrupt", 0, 7393), live)
        self.assertNotIn((532, 61224, "interrupt", 0, 57094), live)

    def test_step_one_schema_and_windows(self):
        # The shared columns of the aura-tactic kinds and the row gates (20261103010000): a health band a row applies
        # in, a bot aura it needs, a per-kind mode word, and the linked_death window. The primary key gains the band, so
        # one boss can carry the same kind once per band. A gated row is closed while the master key is off; that is
        # the C++ policy's business (tools/test_boss_tactics_policy.cpp), here only the table's shape is pinned.
        path = os.path.join(ROOT, "data/sql/world/20261103010000_world.sql")
        self.assertTrue(os.path.isfile(path), "the row-gate migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for column, definition in (
            ("hp_min", "tinyint unsigned NOT NULL DEFAULT 0"),
            ("hp_max", "tinyint unsigned NOT NULL DEFAULT 100"),
            ("bot_aura", "int unsigned NOT NULL DEFAULT 0"),
            ("tactic_mode", "varchar(16) NOT NULL DEFAULT \'\'\'\'"),
            ("window_s", "float NOT NULL DEFAULT 7"),
        ):
            self.assertIn("ADD COLUMN `%s` %s" % (column, definition), text)
            self.assertIn("COLUMN_NAME = '%s'" % column, text)
        self.assertIn("information_schema.STATISTICS", text)
        self.assertIn("DROP PRIMARY KEY, ADD PRIMARY KEY (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, "
                      "`hp_min`, `hp_max`)", text)

        # Thekal's group keeps the 15 s its burn order relies on (the script's retry), every other group the 7 s
        # default. The UPDATE is replayed by effective_rows, so a wrong scope fails here.
        self.assertIn("UPDATE `ai_boss_tactic` SET `window_s` = 15 WHERE `map` = 309 AND `boss_entry` = 14599 "
                      "AND `kind` = 'linked_death';", text)
        live = {(row[1], row[2], row[3], row[4], row[5]): row for row in effective_rows()}
        for entry in (11347, 11348):
            row = live.get((309, 14599, "linked_death", entry, 0))
            self.assertIsNotNone(row, "the Thekal / %s linked_death row is missing" % entry)
            self.assertEqual(15.0, row[13], "Thekal / %s window_s is not 15" % entry)
        for key, row in live.items():
            if key[2] == "linked_death" and key[:2] != (309, 14599):
                self.assertEqual(7.0, row[13], "%s: a new group's window is not the 7 s default" % (key,))

        # Hakkar's Bloodsiphon interrupt can never fire (instant, interruptFlags 0); the key stays deleted.
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24324;", text)
        self.assertNotIn((309, 14834, "interrupt", 0, 24324), live)

        # Every row written before the new columns takes their defaults, so adding the columns changes no existing
        # row's behaviour. Rows from 20261103010000 onward carry their own mode and gate by design.
        for key, row in live.items():
            if key[2] != "linked_death" and row[0] < "20261103010000_world.sql":
                self.assertEqual((0, 100, 0, "", 7.0), (row[9], row[10], row[11], row[12], row[13]),
                                 "%s carries a gate, mode or window unexpectedly" % (key,))

    def test_zg_aura_rows(self):
        # Zul'Gurub's aura rows (20261103020000_world.sql). The core ids are pinned in BossTacticsCoreData below; here
        # the live table shape is, so a delete that is too wide or a priority that drifted fails.
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        expected = {
            (309, 14834, "dispel_priority", 0, 24321): (0, 0.0, 0, "never"),
            (309, 14834, "dispel_priority", 0, 24687): (1, 0.0, 0, "now"),
            (309, 14834, "dispel_priority", 0, 24688): (2, 0.0, 0, "now"),
            (309, 11382, "freeze_on_aura", 0, 24314): (0, 0.0, 0, ""),
            (309, 14988, "hold_kill", 11382, 0): (0, 0.0, 0, ""),
            (309, 11380, "dispel_priority", 0, 17172): (0, 0.0, 0, "now"),
            (309, 11380, "dispel_priority", 0, 24306): (0, 0.0, 0, "never"),
            (309, 11380, "kill_first", 14986, 0): (0, 0.0, 24306, ""),
            (309, 11380, "kill_first", 15112, 0): (1, 0.0, 0, ""),
            (309, 11380, "kill_first", 14987, 0): (2, 0.0, 0, ""),
            (309, 14510, "kill_first", 15041, 0): (1, 0.0, 0, ""),
            (309, 14510, "dispel_priority", 0, 24111): (1, 0.0, 0, "now"),
            (309, 14510, "dispel_priority", 0, 24099): (2, 0.0, 0, "now"),
            (309, 14515, "flee_on_aura", 0, 24210): (0, 5.0, 0, "to_tank"),
            (309, 14515, "dispel_priority", 0, 24212): (3, 0.0, 0, "now"),
            (309, 15114, "behind", 0, 16099): (0, 20.0, 0, ""),
            (309, 15114, "interrupt", 0, 22421): (0, 0.0, 0, ""),
            (309, 15114, "dps_hold_after", 0, 22421): (0, 3.0, 0, ""),
            (309, 15082, "dps_hold_after", 0, 24646): (0, 3.0, 0, ""),
            (309, 15083, "kill_first", 15163, 0): (1, 0.0, 0, ""),
            (309, 15084, "dps_hold_after", 0, 0): (0, 3.0, 0, "blink"),
            (309, 15085, "interrupt", 0, 24680): (0, 0.0, 0, ""),
        }
        for key, want in expected.items():
            self.assertIn(key, live, "missing ZG aura row %s" % (key,))
            row = live[key]
            self.assertEqual(want, (row["priority"], row["param"], row["bot_aura"], row["tactic_mode"]),
                             "ZG row %s drifted" % (key,))
        # The rows this file must not disturb: Mar'li's channel interrupt and Arlokk's Cleave row stay; Hakkar's dead
        # Bloodsiphon interrupt stays deleted.
        self.assertIn((309, 14510, "interrupt", 0, 24300), live)
        self.assertIn((309, 14515, "behind", 0, 26350), live)
        self.assertNotIn((309, 14834, "interrupt", 0, 24324), live)
        # The two spell-less dps_hold_after rows (Hakkar 14834, Mandokir 11382) are gone: a spell-less hold fires only
        # its engage edge, once per fight, so "until_tank" never recurred. Their triggers are auras on players (or an
        # instant cast, Cause Insanity 24327, castingTimeIndex 1), which the hold's cast/aura edge cannot see on the
        # boss. Re-add only with a trigger that recurs (follow-up in docs/play/boss-tactics.md, Known limits).
        self.assertNotIn((309, 14834, "dps_hold_after", 0, 0), live)
        self.assertNotIn((309, 11382, "dps_hold_after", 0, 0), live)

    def test_plan_interrupt_bosses_have_rows(self):
        # pjerra/turtle-agent#254: the turtle-agent plan JSON lists these
        # interrupts, and the module never read them. Fixed table, kept here
        # so the rows cannot silently disappear; do not read the plan repo.
        # Grimlok's Chain Bolt 8292 is in the plan but has no row: the core
        # cannot interrupt it (interruptFlags 0x9 lacks the damage bit).
        expected = [
            (43, 3673, 6778),
            (43, 3673, 700),
            (43, 3673, 9532),
            (43, 3669, 8040),
            (43, 3669, 9532),
            (43, 3669, 5187),
            (43, 3670, 5187),
            (43, 3670, 700),
            (43, 3670, 9532),
            (43, 3674, 6254),
            (43, 3654, 7967),
            (70, 4854, 9532),
            (36, 647, 5208),
        ]
        have = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        for map_id, boss, spell in expected:
            self.assertIn(
                (map_id, boss, "interrupt", 0, spell),
                have,
                "missing ai_boss_tactic interrupt row map %s boss %s spell %s" % (map_id, boss, spell),
            )

    def test_bat_rider_explosion_row(self):
        # The Gurubashi Bat Rider (14750) casts Unstable Concoction 24024 on itself below
        # 40 % health (core boss_jeklik.cpp npc_guru_bat_riderAI): a 3 s cast, fire damage to
        # every enemy within 10 yd (radius index 13). A kick cannot stop it (preventionType 0,
        # interruptFlags 9) and the template is stun-immune, so the row is avoid_cast and its
        # radius is the blast's 10 yd plus 3.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])): float(row[7]) for row in effective_rows()}
        self.assertIn((309, 14750, "avoid_cast", 0, 24024), live)
        self.assertGreaterEqual(live[(309, 14750, "avoid_cast", 0, 24024)], 13.0)
        self.assertNotIn((309, 14750, "interrupt", 0, 24024), live)

    def test_venoxis_mad_servant_and_voidwalker_rows(self):
        # Zul'Gurub (309), the High Priest Venoxis leg's Mad Servant / Mad Voidwalker wall. A dying Mad
        # Servant 15111 leaves a Portal of Madness that summons two Mad Voidwalkers 15146, which channel
        # Consuming Shadows 24614 (radius index 8 = 5 yd, 400 shadow a second). The row is avoid_cast at
        # 5 + 3 yd: the scan reads the channel slot, and a kick does stop the channel. Flamestrike 24612 is
        # the Servant's kickable cast (castingTimeIndex 5, interruptFlags 15); Fireball 24611 gets no row
        # (a 0.5 s cast the first-castable-wins scan would spend the Flamestrike kicks on).
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])): float(row[7]) for row in effective_rows()}
        self.assertIn((309, 15146, "avoid_cast", 0, 24614), live)
        self.assertEqual(8.0, live[(309, 15146, "avoid_cast", 0, 24614)])
        self.assertIn((309, 15111, "interrupt", 0, 24612), live)
        self.assertNotIn((309, 15146, "interrupt", 0, 24614), live)
        self.assertNotIn((309, 15111, "interrupt", 0, 24611), live)
        self.assertFalse([key for key in live if key[:2] == (309, 15146) and key[2] == "kill_first"])

    def test_voidwalker_tank_hold_row(self):
        # A tank_hold row (20261101040000_world.sql) is opt-in per caster: it means "a tank may stay in this caster's
        # avoid_cast blast while the caster is on a non-tank", so the tank can pick the Mad Voidwalker 15146 up and
        # hold it in melee. Without it a tank only holds a caster that is on the tank itself. The Gurubashi Bat Rider
        # 14750 gets none: its Unstable Concoction blast kills whoever stands in it, so no tank may hold it.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        self.assertIn((309, 15146, "tank_hold", 0, 0), live)
        self.assertFalse([key for key in live if key[2] == "tank_hold" and key[1] == 14750])

    def test_one_at_a_time_paces_the_mad_servants(self):
        # ZG walk 22: even with both hold_kill rows in, 3-4 Mad Servants of one wave were already engaged when the
        # first died, and the free DPS split over 2-3 of them. one_at_a_time (map 309, Servant 15111) sends every DPS
        # to the same Servant: the lowest health, the lowest guid on a tie.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        self.assertIn((309, 15111, "one_at_a_time", 0, 0), live)

        # The migration that writes it DELETEs the (309, 15111) key first and re-inserts the interrupt row and both
        # hold_kill rows from 20261101050000 byte-identical, the shape test_dungeon_plan_sql.py requires of a
        # re-INSERTed key.
        path = os.path.join(ROOT, "data/sql/world/20261101070000_world.sql")
        self.assertTrue(os.path.isfile(path), "the one_at_a_time migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        delete = text.index("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15111);")
        self.assertLess(delete, text.index("INSERT INTO `ai_boss_tactic`"))
        with open(os.path.join(ROOT, "data/sql/world/20261101050000_world.sql"), encoding="utf-8") as handle:
            earlier = handle.read()
        kept = [line for line in earlier.splitlines() if line.startswith("(309, 15111,")]
        self.assertEqual(len(kept), 3)
        for line in kept:
            self.assertIn(line.rstrip(",;") + ",", text,
                          "a Mad Servant row did not go back in unchanged")
        self.assertIn("(309, 15111, 'one_at_a_time', 0, 0, 0, 0, '", text)

    def test_linked_death_pairs_the_thekal_zealots(self):
        # Walk 27 C: Thekal 14599 and the Zealots Lor'Khan 11347 and Zath 11348 raise a dead sibling to full health 7 s
        # after the death, so they have to die together. The two rows of one (boss_entry, priority) key are the group;
        # param 2 % leaves ~6.5 k hp for the release burst, 2-3 s at the walk's 2.2-3.0 k dps (inside the 7 s timer),
        # and the burn order is the raiser chain: Lor'Khan (raised by Zath), then Zath (raised by Thekal), then Thekal.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])):
                (int(row[6]), float(row[7]), row[8])
                for row in effective_rows()}
        self.assertEqual((1, 2.0, "11347,11348,14599"), live.get((309, 14599, "linked_death", 11347, 0)),
                         "the Thekal / Lor'Khan linked_death row is missing or changed")
        self.assertEqual((1, 2.0, "11347,11348,14599"), live.get((309, 14599, "linked_death", 11348, 0)),
                         "the Thekal / Zath linked_death row is missing or changed")
        self.assertNotIn((309, 14599, "linked_death", 0, 0), live)       # the anchor is no row of its own
        self.assertNotIn((309, 11347, "linked_death", 14599, 0), live)   # partners are not anchors
        path = os.path.join(ROOT, "data/sql/world/20261102210000_world.sql")
        self.assertTrue(os.path.isfile(path), "the Thekal burn-order migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("ADD COLUMN `burn_order` varchar(96) NOT NULL DEFAULT '''' AFTER `param`", text)
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14599) "
                      "AND `kind` = 'linked_death';", text)
        # The two group rows carry the same order: the loader takes the first non-empty one, so they cannot diverge.
        rows = [line for line in text.splitlines() if line.startswith("(309, 14599, 'linked_death',")]
        self.assertEqual(2, len(rows))
        for line in rows:
            self.assertIn("2.0, '11347,11348,14599'", line)

    def test_the_thekal_heal_is_interrupted(self):
        # Lor'Khan's Greater Heal 24208 (boss_thekal.cpp) keeps the lowest friendly under 9500 hp alive every 12 s,
        # which makes staggered kills worse; spell_template interruptFlags 15 makes it kickable.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        self.assertIn((309, 11347, "interrupt", 0, 24208), live)
        with open(os.path.join(ROOT, "data/sql/world/20261102210000_world.sql"), encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11347) "
                      "AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24208;", text)

    def test_the_linked_death_delete_spares_another_kind(self):
        # The anchor 14599 can carry other tactic rows (a kill_first on a Zealot, an interrupt); re-applying this
        # migration may only replace its own linked_death rows. The DELETE is parsed and replayed here, so dropping the
        # kind scope (a boss-wide DELETE) leaves the other row gone and fails.
        path = os.path.join(ROOT, "data/sql/world/20261102200000_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        line = next(line for line in text.splitlines() if DELETE_LINE.match(line))
        match = DELETE.match(line)
        self.assertIsNotNone(match, line)
        self.assertEqual("linked_death", match.group(4))
        live = {
            (309, 14599, "linked_death", 11347, 0),
            (309, 14599, "linked_death", 11348, 0),
            (309, 14599, "kill_first", 11361, 0),
        }
        scope = delete_scope(match)
        live = {key for key in live if not delete_covers(scope, key)}
        self.assertEqual(live, {(309, 14599, "kill_first", 11361, 0)})


def _sql_statements(text):
    """Split a dump into statements: comments dropped, quotes and escapes respected."""
    statements = []
    buffer = []
    index = 0
    size = len(text)
    quote = None
    while index < size:
        char = text[index]
        if quote:
            buffer.append(char)
            if char == "\\" and quote != "`":
                if index + 1 < size:
                    buffer.append(text[index + 1])
                index += 2
                continue
            if char == quote:
                quote = None
            index += 1
            continue
        if char in "'\"`":
            quote = char
            buffer.append(char)
            index += 1
            continue
        if char == "-" and text.startswith("--", index):
            end = text.find("\n", index)
            index = size if end < 0 else end + 1
            continue
        if char == "#":
            end = text.find("\n", index)
            index = size if end < 0 else end + 1
            continue
        if char == "/" and text.startswith("/*", index):
            end = text.find("*/", index + 2)
            index = size if end < 0 else end + 2
            continue
        if char == ";":
            statement = "".join(buffer).strip()
            if statement:
                statements.append(statement)
            buffer = []
            index += 1
            continue
        buffer.append(char)
        index += 1
    statement = "".join(buffer).strip()
    if statement:
        statements.append(statement)
    return statements


def _create_table_fields(text, table, path):
    start = text.find("CREATE TABLE `%s`" % table)
    if start < 0:
        raise AssertionError("no CREATE TABLE for %s in %s" % (table, path))
    ends = [end for end in (text.find("PRIMARY KEY", start), text.find("ROW_FORMAT", start), text.find("ENGINE=", start)) if end > start]
    end = min(ends) if ends else len(text)
    return re.findall(r"^\s*`([A-Za-z_0-9]+)`", text[start:end], re.M)


def _tuples(values):
    """Yield each parenthesised tuple of a VALUES list, outer parentheses stripped."""
    depth = 0
    current = []
    quote = None
    escaped = False
    for char in values:
        if quote:
            current.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "'\"":
            quote = char
            current.append(char)
        elif char == "(":
            depth += 1
            if depth == 1:
                current = []
            else:
                current.append(char)
        elif char == ")":
            depth -= 1
            if depth == 0:
                yield "".join(current)
            else:
                current.append(char)
        elif depth > 0:
            current.append(char)


def _selected_values(tuple_text, indexes):
    """The values at the given indexes; scanning stops after the last wanted one."""
    if not indexes:
        return {}
    last = max(indexes)
    values = []
    current = []
    quote = None
    escaped = False
    for char in tuple_text:
        if quote:
            current.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "'\"":
            quote = char
            current.append(char)
        elif char == ",":
            values.append("".join(current))
            current = []
            if len(values) > last:
                break
        else:
            current.append(char)
    else:
        values.append("".join(current))
    return {index: values[index] for index in indexes if index < len(values)}


def _value(text):
    text = text.strip()
    if text == "NULL" or text == "":
        return None
    if text.startswith("'"):
        return text[1:-1].replace("\\'", "'").replace('\\"', '"').replace("\\\\", "\\")
    try:
        return int(text)
    except ValueError:
        try:
            return float(text)
        except ValueError:
            return text


def _apply_dump(text, table, fields, wanted):
    rows = []
    header = re.compile(r"(?:INSERT|REPLACE)\s+INTO\s+`" + re.escape(table) + r"`", re.IGNORECASE)
    full = re.compile(
        r"(?:INSERT|REPLACE)\s+INTO\s+`" + re.escape(table) + r"`\s*(?:\(([^)]*)\))?\s*VALUES\s*(.*)$",
        re.IGNORECASE | re.S,
    )
    for statement in _sql_statements(text):
        if not header.match(statement):
            continue
        match = full.match(statement)
        if not match:
            continue
        columns = [column.strip().strip("`") for column in match.group(1).split(",")] if match.group(1) else fields
        indexes = {name: index for index, name in enumerate(columns) if name in wanted}
        for tuple_text in _tuples(match.group(2)):
            values = _selected_values(tuple_text, set(indexes.values()))
            rows.append({name: _value(values[index]) for name, index in indexes.items() if index in values})
    return rows


def _read_core_table(core, filename, table, wanted):
    """One core table: the base dump plus sql/database_updates/world, in file order."""
    base_path = os.path.join(core, "sql", "base", filename)
    with open(base_path, encoding="utf-8", errors="replace") as handle:
        base_text = handle.read()
    fields = _create_table_fields(base_text, table, base_path)
    rows = _apply_dump(base_text, table, fields, wanted)
    updates = os.path.join(core, "sql", "database_updates", "world")
    writes_table = re.compile(r"(?:INSERT|REPLACE)\s+INTO\s+`" + re.escape(table) + r"`", re.IGNORECASE)
    for path in sorted(glob.glob(os.path.join(updates, "*.sql"))):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        if writes_table.search(text):
            rows.extend(_apply_dump(text, table, fields, wanted))
    return rows


# The spell_template columns the interrupt rule reads (the audit's four plus
# attributesEx, which picks the channel branch).
RULE_COLUMNS = {"attributesEx", "castingTimeIndex", "interruptFlags", "channelInterruptFlags", "preventionType"}


def _spell_rule_updates(updates_dir):
    """(file, column) for every UPDATE of a rule column, which the reader would miss."""
    hits = []
    for path in sorted(glob.glob(os.path.join(updates_dir, "*.sql"))):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        if "spell_template" not in text:
            continue
        for statement in _sql_statements(text):
            match = re.match(r"UPDATE\s+`?spell_template`?\s+SET\s+(.*?)(?:\s+WHERE\s|$)", statement, re.IGNORECASE | re.S)
            if not match:
                continue
            for column in re.findall(r"`([A-Za-z_0-9]+)`\s*=", match.group(1)):
                if column in RULE_COLUMNS:
                    hits.append((os.path.basename(path), column))
    return sorted(hits)


CORE = os.environ.get("TORTOISE_CORE")
CORE_SPELLS = os.path.join(CORE or "", "sql", "base", "tw_world_spell_template.sql")


@unittest.skipUnless(CORE and os.path.isfile(CORE_SPELLS), "set TORTOISE_CORE=<tortoise-wow checkout>")
class BossTacticsCoreData(unittest.TestCase):
    SPELL_COLUMNS = ("entry", "attributesEx", "castingTimeIndex", "interruptFlags",
                     "channelInterruptFlags", "preventionType", "dispel",
                     "effect1", "effect2", "effect3", "effectImplicitTargetA1", "effectImplicitTargetA2",
                     "effectImplicitTargetA3", "effectApplyAuraName1", "effectApplyAuraName2",
                     "effectApplyAuraName3")
    # Creatures the core puts on a map without a creature row: the summon that makes them.
    SUMMONED = {
        3654: "src/scripts/dungeons/wailing_caverns/wailing_caverns.cpp:504 "
              "(SummonAttacker in the Disciple of Naralex event)",
        7275: "src/scripts/dungeons/zulfarrak/instance_zulfarrak.cpp:81 (the stairs event summons Shadowpriest Sezz'ziz)",
        7273: "sql/base/tw_world_event_scripts.sql:58, event 2488 (the eventId of the "
              "Gong of Zul'Farrak 141832, tw_world_gameobject_template.sql:72) summons him",
        15146: "sql/base/tw_world_creature_ai_events.sql:52, event 1514101 (creature 15141, the Portal of Madness "
               "a dying Mad Servant 15111 leaves behind with 24621) casts 24620 every 30 s, whose 24622 summons "
               "2 Mad Voidwalkers",
        14515: "sql/base/tw_world_event_scripts.sql:58, event 9066 (the eventId of the Gong of Bethekk 180526, tw_world_gameobject_template.sql:72) summons her",
        12018: "src/scripts/dungeons/molten_core/instance_molten_core.cpp:652 (SummonCreature(NPC_DOMO) once the seven runes are doused)",
        15517: "src/scripts/dungeons/temple_of_ahnqiraj/boss_ouro.cpp:462 (the Ouro spawner casts SPELL_SUMMON_OURO 26061)",
        14988: "src/scripts/dungeons/zulgurub/boss_mandokir.cpp:333 (Mandokir summons Ohgan onto his victim)",
        14986: "src/scripts/dungeons/zulgurub/boss_jindo.cpp:284 (SPELL_SHADE_OF_JINDO 24308 summons the Shade)",
        15112: "src/scripts/dungeons/zulgurub/boss_jindo.cpp:191 (SPELL_BRAIN_WASH_TOTEM 24262 summons the totem)",
        15041: "src/scripts/dungeons/zulgurub/boss_marli.cpp:146 (Mar'li summons a Spawn of Mar'li from an egg)",
        15163: "src/scripts/dungeons/zulgurub/boss_hazzarah.cpp:86 (Hazza'rah summons a Nightmare Illusion)",
        15082: "sql/base/tw_world_gameobject_scripts.sql, brazier 180327 script 28704 command 10 summons Gri'lek",
        15083: "sql/base/tw_world_gameobject_scripts.sql, brazier 180327 script 21608 command 10 summons Hazza'rah",
        15084: "sql/base/tw_world_gameobject_scripts.sql, brazier 180327 script 21609 command 10 summons Renataki",
        15085: "sql/base/tw_world_gameobject_scripts.sql, brazier 180327 script 21610 command 10 summons Wushoolay",
    }

    @classmethod
    def setUpClass(cls):
        cls.spells = {row["entry"]: row for row in _read_core_table(CORE, "tw_world_spell_template.sql", "spell_template", cls.SPELL_COLUMNS)}
        cls.templates = {row["entry"] for row in _read_core_table(CORE, "tw_world_creature_template.sql", "creature_template", ("entry",))}
        cls.spawns = set()
        for row in _read_core_table(CORE, "tw_world_creature.sql", "creature", ("id", "id2", "id3", "id4", "map")):
            for entry in (row.get("id"), row.get("id2"), row.get("id3"), row.get("id4")):
                if entry:
                    cls.spawns.add((row.get("map"), entry))
        cls.rule_updates = _spell_rule_updates(os.path.join(CORE, "sql", "database_updates", "world"))

    def interrupt_rows(self):
        return sorted((int(row[1]), int(row[2]), int(row[5])) for row in effective_rows() if row[3] == "interrupt")

    def test_interrupt_spells_exist(self):
        for map_id, boss, spell in self.interrupt_rows():
            self.assertIn(spell, self.spells, "map %s boss %s interrupt spell %s has no spell_template row" % (map_id, boss, spell))

    def test_interrupt_spells_are_interruptible(self):
        # Mirrors Spell::EffectInterruptCast (src/game/Spells/SpellEffects.cpp:3389-3393):
        # PreventionType must be SILENCE (1); a generic cast needs a cast time and
        # SPELL_INTERRUPT_FLAG_DAMAGE (0x02); a channel needs CHANNEL_FLAG_INTERRUPT (0x04).
        problems = []
        for map_id, boss, spell in self.interrupt_rows():
            where = "map %s boss %s spell %s" % (map_id, boss, spell)
            info = self.spells.get(spell)
            if info is None:
                problems.append("%s: no spell_template row" % where)
            elif info["preventionType"] != 1:
                problems.append("%s: preventionType %s is not SILENCE (1); the kick cannot stop it"
                                % (where, info["preventionType"]))
            elif info["attributesEx"] & 0x44:
                if not info["channelInterruptFlags"] & 0x4:
                    problems.append("%s: channeled (attributesEx 0x%x) but channelInterruptFlags 0x%x lacks 0x4"
                                    % (where, info["attributesEx"], info["channelInterruptFlags"]))
            elif info["castingTimeIndex"] <= 1:
                problems.append("%s: castingTimeIndex %s, the cast is instant" % (where, info["castingTimeIndex"]))
            elif not info["interruptFlags"] & 0x2:
                problems.append("%s: interruptFlags 0x%x lacks SPELL_INTERRUPT_FLAG_DAMAGE 0x2"
                                % (where, info["interruptFlags"]))
        self.assertEqual([], problems, "the core cannot interrupt these rows:\n" + "\n".join(problems))

    def test_aura_tactic_spells_exist(self):
        # Every spell a new-kind row names exists in spell_template: a dispel_priority aura, a freeze/flee aura, a
        # dps_hold_after trigger or a kill_first target's spell id (0 for the target rows).
        aura_kinds = ("dispel_priority", "freeze_on_aura", "flee_on_aura", "dps_hold_after")
        problems = []
        for row in effective_rows():
            if row[3] not in aura_kinds or not int(row[5]):
                continue
            if int(row[5]) not in self.spells:
                problems.append("map %s boss %s kind %s spell %s has no spell_template row"
                                % (row[1], row[2], row[3], row[5]))
        self.assertEqual([], problems, "aura tactic spells the core does not know:\n" + "\n".join(problems))

    def test_dispel_priority_auras_are_curable(self):
        # Only the four friendly dispel types exist in 1.12 (magic, curse, disease, poison); a row on an aura with
        # dispel 0 is a no-op (Corrupted Blood 24328, Brain Wash 24261 are the classic traps).
        problems = []
        for row in effective_rows():
            if row[3] != "dispel_priority":
                continue
            info = self.spells.get(int(row[5]))
            if info is None:
                problems.append("map %s boss %s aura %s: no spell_template row" % (row[1], row[2], row[5]))
            elif info["dispel"] not in (1, 2, 3, 4):
                problems.append("map %s boss %s aura %s: dispel %s is not curable"
                                % (row[1], row[2], row[5], info["dispel"]))
        self.assertEqual([], problems, "dispel_priority rows a cure cannot take:\n" + "\n".join(problems))

    def test_dps_hold_triggers_are_observable(self):
        # The hold's spell trigger is a cast slot or an aura on the subject, so a keyed spell must be a cast (cast
        # index > 1) or an aura applied to the caster; an instant, aura-less spell is invisible to polling and the row
        # must use `blink` or `until_tank` instead.
        problems = []
        for row in effective_rows():
            if row[3] not in ("dps_hold_after", "taunt_after_cast") or not int(row[5]):
                continue
            if row[12] in ("blink", "until_tank"):
                continue
            info = self.spells.get(int(row[5]))
            where = "map %s boss %s kind %s spell %s" % (row[1], row[2], row[3], row[5])
            if info is None:
                problems.append("%s: no spell_template row" % where)
                continue
            self_aura = any(
                info.get("effectApplyAuraName%d" % index) and info.get("effectImplicitTargetA%d" % index) == 1
                for index in (1, 2, 3))
            if info["castingTimeIndex"] <= 1 and not self_aura:
                problems.append("%s: castingTimeIndex %s and no self aura; use blink or until_tank"
                                % (where, info["castingTimeIndex"]))
        self.assertEqual([], problems, "dps_hold_after rows the trigger cannot see:\n" + "\n".join(problems))

    def test_avoid_cast_spells_are_casts(self):
        # The avoid_cast scan reads the caster's current generic spell and its channel, so a row is good when the
        # spell is a cast (a cast time) or a channel (attributesEx 0x44): an instant spell never stays in either slot
        # long enough to be seen.
        rows = sorted((int(row[1]), int(row[2]), int(row[5])) for row in effective_rows() if row[3] == "avoid_cast")
        problems = []
        for map_id, boss, spell in rows:
            info = self.spells.get(spell)
            if info is None:
                problems.append("map %s boss %s spell %s: no spell_template row" % (map_id, boss, spell))
            elif info["castingTimeIndex"] <= 1 and not info["attributesEx"] & 0x44:
                problems.append("map %s boss %s spell %s: castingTimeIndex %s and attributesEx 0x%x, neither a cast "
                                "nor a channel" % (map_id, boss, spell, info["castingTimeIndex"], info["attributesEx"]))
        self.assertEqual([], problems, "avoid_cast rows the scan cannot see:\n" + "\n".join(problems))

    def test_boss_and_target_entries_exist(self):
        for row in effective_rows():
            where = "map %s boss %s kind %s" % (row[1], row[2], row[3])
            self.assertIn(int(row[2]), self.templates, "%s: boss_entry has no creature_template row" % where)
            if int(row[4]):
                self.assertIn(int(row[4]), self.templates, "%s: target_entry %s has no creature_template row" % (where, row[4]))

    def test_bosses_spawn_on_their_map(self):
        for row in effective_rows():
            map_id, boss = int(row[1]), int(row[2])
            if boss in self.SUMMONED:
                continue
            self.assertIn((map_id, boss), self.spawns,
                          "map %s boss %s has no creature row with its entry on that map (and no SUMMONED reason)" % (map_id, boss))

    def test_spell_table_updates_do_not_touch_the_rule(self):
        self.assertEqual(
            [],
            self.rule_updates,
            "database_updates/world UPDATEs spell_template columns the interrupt rule reads; extend the reader",
        )


if __name__ == "__main__":
    unittest.main()
