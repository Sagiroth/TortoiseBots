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
DOC = os.path.join(ROOT, "docs/play/boss-tactics.md")
KINDS = {"interrupt", "kill_first", "behind", "avoid_cast", "tank_hold", "hold_kill", "one_at_a_time",
         "linked_death", "dps_hold_after", "dispel_priority", "flee_on_aura", "freeze_on_aura",
         "taunt_after_cast", "swap_on_aura", "use_object", "seek_aura", "pull_gate", "spread", "needs_aura",
         "burn_mana", "ranged_only", "threat_successor", "anchor", "tank_ignore", "seek_pull", "gate_hold", "tank_post"}
# Kinds read straight from the table (the dps and tank pickers, the linked_death scan), never from a fight scan: a row
# of one of them cannot answer a gate, so the loader refuses one.
TABLE_READ_KINDS = {"hold_kill", "tank_hold", "one_at_a_time", "linked_death", "threat_successor", "tank_ignore"}
# The mode word each kind takes (`tactic_mode`); an empty word is the kind's own behaviour.
MODE_WORDS = {
    "dispel_priority": {"now", "clear", "never"},
    "flee_on_aura": {"away", "to_tank"},
    "dps_hold_after": {"engage", "blink", "until_tank"},
    "taunt_after_cast": {"engage", "blink", "until_tank"},
    "use_object": {"tank_anchor"},
    "avoid_cast": {"all"},
    "threat_successor": {"strict"},
}
# A row as the replay hands it around: (file,) + the table columns in this order.
FIELDS = ("file", "map", "boss_entry", "kind", "target_entry", "spell_id", "priority", "param", "burn_order",
          "hp_min", "hp_max", "bot_aura", "tactic_mode", "window_s", "cycle_spell", "cycle_s", "spot_x", "spot_y",
          "spot_z")
INSERT_COLUMNS = {
    "map": 0, "boss_entry": 0, "kind": "", "target_entry": 0, "spell_id": 0, "priority": 0, "param": 0.0,
    "burn_order": None, "comment": "", "hp_min": 0, "hp_max": 100, "bot_aura": 0, "tactic_mode": "",
    "window_s": 7.0, "cycle_spell": 0, "cycle_s": 0.0, "spot_x": 0.0, "spot_y": 0.0, "spot_z": 0.0,
}
DELETE = re.compile(
    r"^DELETE FROM `ai_boss_tactic` WHERE `map` = (\d+)"
    r"(?: AND `boss_entry` IN \(([\d, ]+)\)| AND `boss_entry` = (\d+))?"
    r"(?: AND `kind` = '(\w+)')?"
    r"(?: AND `target_entry` = (\d+) AND `spell_id` = (\d+))?"
    r"(?: AND `hp_min` = (\d+) AND `hp_max` = (\d+))?;?$"
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
    row["cycle_spell"] = int(row["cycle_spell"] or 0)
    row["cycle_s"] = float(row["cycle_s"] or 0.0)
    for column in ("spot_x", "spot_y", "spot_z"):
        row[column] = float(row[column] or 0.0)
    return row


def _payload(row, name):
    row = dict(row)
    row["file"] = name
    return tuple(row[field] for field in FIELDS)


def _payload_dict(payload):
    return dict(zip(FIELDS, payload))


def migration_statements(paths=None):
    """Yield ("insert", payload) and ("delete", (name, match)) in file and statement order."""
    for path in (migration_paths() if paths is None else paths):
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


def migration_updates(paths=None):
    """The scoped `UPDATE ai_boss_tactic SET ... WHERE ...` statements the replay applies, in file order."""
    updates = []
    for path in (migration_paths() if paths is None else paths):
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
    for part in re.split(r"\s+AND\s+", text.strip(), flags=re.I):
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
    """(map, bosses, single entry, kind, target, spell, hp_min, hp_max) of a parsed DELETE, None when absent."""
    return (
        int(match.group(1)),
        {int(entry) for entry in match.group(2).split(",")} if match.group(2) else None,
        int(match.group(3)) if match.group(3) else None,
        match.group(4),
        int(match.group(5)) if match.group(5) else None,
        int(match.group(6)) if match.group(6) else None,
        int(match.group(7)) if match.group(7) else None,
        int(match.group(8)) if match.group(8) else None,
    )


def delete_covers(scope, key):
    """Whether the parsed DELETE removes one (map, boss_entry, kind, target_entry, spell_id[, hp_min, hp_max]) key."""
    map_id, bosses, entry, kind, target, spell, hp_min, hp_max = scope
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
    if hp_min is not None and (len(key) < 7 or key[5] != hp_min or key[6] != hp_max):
        return False
    return True


def effective_rows(paths=None):
    """The rows a boot ends up with: every INSERT, minus the DELETEs that follow it, plus the scoped UPDATEs.

    `paths` replays only those migration files, in the order given (default: every file, in name order).
    """
    live = {}
    for kind, payload in migration_statements(paths):
        if kind == "insert":
            # The key includes the health band, so one boss carries the same kind once per band.
            live[(payload[1], payload[2], payload[3], payload[4], payload[5], payload[9], payload[10])] = payload
            continue
        _name, match = payload
        scope = delete_scope(match)
        for key in [key for key in live if delete_covers(scope, key)]:
            del live[key]
    rows = list(live.values())
    for _name, changes, wanted in migration_updates(paths):
        for index, payload in enumerate(rows):
            row = _payload_dict(payload)
            if _matches(row, wanted):
                row.update(changes)
                rows[index] = _payload(row, payload[0])
    return rows


def new_kind_problem(row):
    """BossTacticTable::Add's refusals of a seek_aura, pull_gate or spread row, or None for a usable one."""
    kind = row["kind"]
    has_spot = any(row[column] for column in ("spot_x", "spot_y", "spot_z"))
    if kind in ("seek_aura", "pull_gate", "spread", "burn_mana", "ranged_only", "threat_successor", "anchor",
                "tank_ignore", "seek_pull", "gate_hold") and (
            row["hp_min"] or row["hp_max"] != 100 or row["bot_aura"]):
        return "a health band or bot aura on a kind no fight scan asks"
    if kind == "gate_hold":
        # The spared entry (boss_entry) and its blocker (target_entry): `param` the health gate 1-99, `priority` the
        # blockers that may still fight, `window_s` the search radius (0 = 200 yd, else 50-250). The row's own health
        # gate is `param`, so the band columns, a spell, a spot, a mode word and the cycle columns are refused.
        if not row["target_entry"]:
            return "gate_hold without its blocker"
        if row["target_entry"] == row["boss_entry"]:
            return "gate_hold blocker is its own boss"
        if row["spell_id"]:
            return "gate_hold with a spell"
        if not 1.0 <= row["param"] <= 99.0:
            return "gate_hold gate outside [1, 99]"
        if not (row["window_s"] == 0.0 or 50.0 <= row["window_s"] <= 250.0):
            return "gate_hold radius outside 0 or [50, 250]"
        if row["tactic_mode"]:
            return "a mode word on gate_hold"
        if row["cycle_spell"] or row["cycle_s"] or has_spot:
            return "cycle or spot columns on another kind"
        return None
    if kind == "burn_mana":
        # target_entry 0 (the boss), param the start % (exclusive), priority the most burners (0 = every eligible
        # bot, so 0..10).
        if row["target_entry"]:
            return "burn_mana with a target"
        if not 0.0 < row["param"] < 100.0:
            return "burn_mana start percent outside (0, 100)"
        if row["priority"] > 10:
            return "burn_mana above ten burners"
        if row["cycle_spell"] or row["cycle_s"] or has_spot:
            return "cycle or spot columns on another kind"
        return None
    if kind == "ranged_only":
        # target_entry 0 (the boss) or an entry; param is the vertical gap the melee bot is held under.
        if not 3.0 <= row["param"] <= 40.0:
            return "ranged_only gap outside [3, 40]"
        if row["cycle_spell"] or row["cycle_s"] or has_spot:
            return "cycle or spot columns on another kind"
        return None
    if kind == "seek_aura":
        if not row["target_entry"] or not row["spell_id"] or not row["cycle_spell"] or not has_spot:
            return "seek_aura without its source, its aura, its cycle spell or its spot"
        if not 0.0 < row["param"] <= 10.0:
            return "seek_aura gather radius outside (0, 10]"
        if row["priority"] > 99:
            return "seek_aura hold percent above 99"
        if not 0.0 < row["cycle_s"] <= 600.0:
            return "seek_aura cycle length outside (0, 600]"
        if not 0.0 < row["window_s"] <= row["cycle_s"]:
            return "seek_aura window outside (0, cycle]"
        return None
    if kind == "pull_gate":
        if row["target_entry"] or not row["spell_id"] or not 0.0 < row["param"] <= 300.0:
            return "pull_gate with a target, without a spell or with a hold outside (0, 300]"
        return None
    if kind == "spread":
        if row["target_entry"] or not 5.0 <= row["param"] <= 30.0:
            return "spread with a target or a spacing outside [5, 30]"
        return None
    if kind == "threat_successor":
        # The boss itself, and nothing else: the kind is read straight from the table by the threat reference.
        if row["target_entry"]:
            return "threat_successor with a target"
        if row["tactic_mode"] not in ("", "strict"):
            return "a mode word threat_successor does not take (only strict)"
        if row["cycle_s"]:
            return "cycle_s on threat_successor"
        if row["cycle_spell"] and not (5.0 <= row["param"] < row["window_s"] <= 60.0
                                       and 50 <= row["priority"] <= 100):
            return "a reset window outside 5 <= param < window_s <= 60 or priority outside 50-100"
        return None
    if kind == "seek_pull":
        # The seek source (target_entry), the most walk-outs in one cycle (priority), the kill lead (param) and the
        # last-start window. No spell, spot, cycle columns, gate or mode word.
        if not row["target_entry"]:
            return "seek_pull without its source"
        if row["spell_id"]:
            return "seek_pull with a spell"
        if not 1 <= row["priority"] <= 5:
            return "seek_pull cycle walk-outs outside [1, 5]"
        if not 10.0 <= row["param"] <= 85.0:
            return "seek_pull kill lead outside [10, 85]"
        if not 10.0 <= row["window_s"] <= 85.0:
            return "seek_pull last-start window outside [10, 85]"
        if row["tactic_mode"]:
            return "a mode word on seek_pull"
        if row["cycle_spell"] or row["cycle_s"] or has_spot:
            return "cycle or spot columns on another kind"
        return None
    if kind == "tank_ignore":
        # The entry the tanks leave to the damage dealers while the boss fights, and nothing else (read straight from
        # the table by the tank pickers and the run guard).
        if not row["target_entry"]:
            return "tank_ignore without its target"
        if row["spell_id"]:
            return "tank_ignore with a spell"
        if row["tactic_mode"]:
            return "a mode word on tank_ignore"
        if row["cycle_spell"] or row["cycle_s"] or has_spot:
            return "cycle or spot columns on another kind"
        return None
    if kind == "anchor":
        # The boss itself (target_entry 0, no spell): spot_x/y/z the centre, param the radius for everyone (10-60 yd),
        # priority the tanks' radius (0 = param, never wider), no mode word and no cycle columns.
        if row["target_entry"] or row["spell_id"]:
            return "anchor with a target or a spell"
        if not has_spot:
            return "anchor without its centre"
        if not 10.0 <= row["param"] <= 60.0:
            return "anchor radius outside [10, 60]"
        if row["priority"] > row["param"]:
            return "anchor tank radius wider than the radius"
        if row["tactic_mode"]:
            return "a mode word on anchor"
        if row["cycle_spell"] or row["cycle_s"]:
            return "cycle columns on another kind"
        return None
    if kind == "tank_post":
        # The awaited unit (target_entry), the tank slot (priority 1-7), the wait spot, the arrival radius (5-40 yd)
        # and the longest hold (window_s 10-120 s). No spell, no mode word, no cycle columns.
        if not row["target_entry"]:
            return "tank_post without its awaited unit"
        if row["spell_id"]:
            return "tank_post with a spell"
        if not 1 <= row["priority"] <= 7:
            return "tank_post tank slot outside 1-7"
        if not has_spot:
            return "tank_post without its spot"
        if not 5.0 <= row["param"] <= 40.0:
            return "tank_post arrival radius outside [5, 40]"
        if not 10.0 <= row["window_s"] <= 120.0:
            return "tank_post window outside [10, 120]"
        if row["tactic_mode"]:
            return "a mode word on tank_post"
        if row["cycle_spell"] or row["cycle_s"]:
            return "cycle columns on another kind"
        return None
    if kind == "needs_aura":
        # The aura is the row's gate: it names the entry and the aura a bot must carry, and nothing else.
        if not row["target_entry"] or not row["spell_id"]:
            return "needs_aura without its target or its aura"
        if row["hp_min"] or row["hp_max"] != 100 or row["bot_aura"]:
            return "a health band or bot aura on needs_aura"
        if row["tactic_mode"]:
            return "a mode word on needs_aura"
        return None
    if row["cycle_spell"] or row["cycle_s"] or has_spot:
        return "cycle or spot columns on another kind"
    return None


class BossTacticsSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_tactic rows found")

    def test_rows_are_loadable(self):
        for row in effective_rows():
            name, map_id, boss, kind, target, spell, priority, param, burn_order = row[:9]
            hp_min, hp_max, bot_aura, mode, window_s = row[9:14]
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
            if kind == "use_object":
                self.assertGreater(int(target), 0, where)       # the game object to click
                self.assertLessEqual(int(priority), 2, where)   # the clicker role: free DPS, healer, non-tank
                self.assertGreater(float(param), 0.0, where)   # the boss-to-object range in yards
                self.assertLessEqual(float(param), 60.0, where)
                self.assertTrue(float(window_s) == 0.0 or float(window_s) >= 10.0, where)
                self.assertTrue(int(spell) > 0 or float(window_s) > 0.0, where)
            problem = new_kind_problem(_payload_dict(row))
            self.assertIsNone(problem, "%s: %s" % (where, problem))

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
            seen = {key for key in seen if not delete_covers(scope, key)}

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
        # row's behaviour. Rows from 20261103010000 onward carry their own mode and gate by design. The F4-A
        # migration (20261107100000) sets mode `all` and the early-zone priority on the pre-existing Bat Rider
        # row, so that key is exempt.
        for key, row in live.items():
            if key[2] != "linked_death" and row[0] < "20261103010000_world.sql" and \
                    key != (309, 14750, "avoid_cast", 0, 24024):
                self.assertEqual((0, 100, 0, "", 7.0), (row[9], row[10], row[11], row[12], row[13]),
                                 "%s carries a gate, mode or window unexpectedly" % (key,))

    def test_zg_aura_rows(self):
        # Zul'Gurub's aura rows (20261103020000_world.sql; Jin'do's kill_first priorities are the ones
        # 20261105234000_world.sql leaves: the ward before the totem). The core ids are pinned in BossTacticsCoreData
        # below; here the live table shape is, so a delete that is too wide or a priority that drifted fails.
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        expected = {
            (309, 14834, "dispel_priority", 0, 24321): (0, 0.0, 0, "never"),
            (309, 14834, "dispel_priority", 0, 24687): (1, 0.0, 0, "now"),
            (309, 14834, "dispel_priority", 0, 24688): (2, 0.0, 0, "now"),
            (309, 11382, "freeze_on_aura", 0, 24314): (0, 0.0, 0, ""),
            (309, 11382, "dps_hold_after", 0, 24314): (0, 4.0, 0, "until_tank"),
            (309, 14988, "hold_kill", 11382, 0): (0, 0.0, 0, ""),
            (309, 11380, "dispel_priority", 0, 17172): (0, 0.0, 0, "now"),
            (309, 11380, "dispel_priority", 0, 24306): (0, 0.0, 0, "never"),
            (309, 11380, "kill_first", 14986, 0): (0, 0.0, 24306, ""),
            (309, 11380, "kill_first", 15112, 0): (2, 0.0, 0, ""),
            (309, 11380, "kill_first", 14987, 0): (1, 0.0, 0, ""),
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
        # The spell-less until_tank hold of Mandokir 11382 is gone: a spell-less hold fires only its engage edge, once
        # per fight, so "until_tank" never recurred. Hakkar's opening hold is back in mode `engage`, where that single
        # edge is the whole rule (the 5 s opening threat hold of 20261107120000); Mandokir's would still need a trigger
        # that recurs. Their triggers are auras on players (or an instant cast, Cause Insanity 24327,
        # castingTimeIndex 1), which the hold's cast/aura edge cannot see on the boss.
        hold = live.get((309, 14834, "dps_hold_after", 0, 0))
        self.assertIsNotNone(hold, "the Hakkar opening dps_hold_after row is missing")
        self.assertEqual("engage", hold["tactic_mode"], "the Hakkar opening hold is not an engage-edge hold")
        self.assertNotIn((309, 11382, "dps_hold_after", 0, 0), live)

    def test_kurinnaxx_mortal_wound_swap_row_is_live(self):
        # The tank swap (swap_on_aura): Kurinnaxx's Mortal Wound 25646 at 5 stacks, a rotation of two tanks (the main
        # tank and off-tank 1). The C++ consumer is behind AiPlayerbot.DungeonRunTankSwap; the row itself is data, and
        # the AQ20 migration owns it: a second migration must not write the same row again.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])): row for row in effective_rows()}
        row = live.get((509, 15348, "swap_on_aura", 0, 25646))
        self.assertIsNotNone(row, "the Kurinnaxx swap_on_aura row is gone")
        self.assertEqual("20261104150000_world.sql", row[0], "the AQ20 migration owns the Kurinnaxx swap row")
        self.assertEqual(2, int(row[6]), "the rotation is two tanks")
        self.assertEqual(5.0, float(row[7]), "the swap fires at 5 Mortal Wound stacks")
        self.assertFalse(os.path.isfile(os.path.join(ROOT, "data/sql/world/20261104155900_world.sql")),
                         "the duplicate Kurinnaxx migration is back")

    def test_mandokir_keyed_gaze_hold_migration(self):
        # 20261104040000_world.sql: the keyed until_tank hold on Threatening Gaze 24314 (a real cast, castingTimeIndex 5),
        # the only dps_hold_after row Mandokir has. Its DELETE names that one row, so it is idempotent and touches
        # neither the freeze_on_aura Gaze row nor Ohgan's hold_kill row.
        name = "20261104040000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the keyed Gaze hold migration is missing")
        self.assertGreater(name, "20261103060000_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11382) AND "
                      "`kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 24314;", text)
        self.assertIn("(309, 11382, 'dps_hold_after', 0, 24314, 0, 4, 0, 'until_tank', ", text)
        self.assertEqual(1, text.count("INSERT INTO `ai_boss_tactic`"))
        self.assertEqual(1, text.count("(309, 11382, 'dps_hold_after'"))
    def test_hakkar_mechanics_schema(self):
        # 20261104010000: the seek_aura row's cycle clock and the spot its source is held at. Idempotent, after
        # window_s; the defaults leave every row written before it unchanged.
        path = os.path.join(ROOT, "data/sql/world/20261104010000_world.sql")
        self.assertTrue(os.path.isfile(path), "the hakkar mechanics schema migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for column, definition in (
            ("cycle_spell", "int unsigned NOT NULL DEFAULT 0"),
            ("cycle_s", "float NOT NULL DEFAULT 0"),
            ("spot_x", "float NOT NULL DEFAULT 0"),
            ("spot_y", "float NOT NULL DEFAULT 0"),
            ("spot_z", "float NOT NULL DEFAULT 0"),
        ):
            self.assertIn("ADD COLUMN `%s` %s" % (column, definition), text)
            self.assertIn("COLUMN_NAME = '%s'" % column, text)
        for row in effective_rows():
            if row[0] < "20261104010000_world.sql":
                self.assertEqual((0, 0.0, 0.0, 0.0, 0.0), row[14:19],
                                 "%s carries a cycle or spot unexpectedly" % (row[0],))

    def test_hakkar_rows(self):
        # 20261107200000_world.sql deleted Hakkar's taunt_after_cast row: like Jin'do he is taunt-immune
        # (creature_template 14834 immunity_flags 6), so Cause Insanity's threat reset is answered by the
        # threat_successor cap instead. The four rows of 20261104020000_world.sql minus that row, plus the
        # opening hold of 20261107120000_world.sql. The core ids are pinned in BossTacticsCoreData below;
        # here the live table shape is, so a wrong number or a delete that reaches too far fails.
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        seek = live.get((309, 14834, "seek_aura", 11357, 24321))
        self.assertIsNotNone(seek, "the Hakkar seek_aura row is missing")
        # 20261106030000 moved the spot onto his platform, 15 yd S of his spawn; every other column is the old row's.
        self.assertEqual((30, 4.0, 80.0, 24324, 90.0, -11787.4, -1664.3, 54.0),
                         (seek["priority"], seek["param"], seek["window_s"], seek["cycle_spell"],
                          seek["cycle_s"], seek["spot_x"], seek["spot_y"], seek["spot_z"]),
                         "the Hakkar seek_aura row drifted")
        gate = live.get((309, 14834, "pull_gate", 0, 24692))
        self.assertIsNotNone(gate, "the Hakkar pull_gate row is missing")
        self.assertEqual((0, 30.0, 0), (gate["priority"], gate["param"], gate["cycle_spell"]),
                         "the Hakkar pull_gate row drifted")
        spread = live.get((309, 14834, "spread", 0, 24328))
        self.assertIsNotNone(spread, "the Hakkar spread row is missing")
        self.assertEqual(12.0, spread["param"], "the Hakkar spread row drifted")
        self.assertNotIn((309, 14834, "taunt_after_cast", 0, 0), live,
                         "the Hakkar taunt_after_cast row is back: Hakkar is taunt-immune")
        # The opening hold: 5 s from the first engagement, no spell, mode engage. A spell-less row fires only that
        # edge, which is exactly a pull hold (it never recurs). Hakkar is taunt-immune and the batch's
        # threat_successor cap keeps damage dealers under the off-tank, so this hold answers the pull only.
        hold = live.get((309, 14834, "dps_hold_after", 0, 0))
        self.assertIsNotNone(hold, "the Hakkar opening dps_hold_after row is missing")
        self.assertEqual((0, 5.0, 0, "engage"),
                         (hold["priority"], hold["param"], hold["bot_aura"], hold["tactic_mode"]),
                         "the Hakkar opening dps_hold_after row drifted")
        self.assertEqual("20261107120000_world.sql", live[(309, 14834, "dps_hold_after", 0, 0)]["file"],
                         "the Hakkar D1 migration owns the opening hold")
        for row in (seek, gate, spread, hold):
            self.assertIsNone(new_kind_problem(row))
        # The three dispel_priority rows of 20261103020000 stay, and nothing else of the boss was deleted.
        for spell in (24321, 24687, 24688):
            self.assertIn((309, 14834, "dispel_priority", 0, spell), live)

    def test_hakkar_seek_pull_row(self):
        # 20261107230000_world.sql: the seek walk-out gets one Son per cycle's rules (walk 51). target_entry the
        # source, priority 3 walk-outs a cycle, window_s the 60 s last start; 20261108100000_world.sql replaces the
        # 45 s kill lead with 75 s, so the pulled Son is burned on arrival.
        name = "20261108100000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the seek_pull migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        pull = live.get((309, 14834, "seek_pull", 11357, 0))
        self.assertIsNotNone(pull, "the Hakkar seek_pull row is missing")
        self.assertEqual(name, pull["file"], "the seek_pull migration owns the row")
        self.assertEqual((3, 75.0, 60.0, "", 0, 0.0, 0, 100),
                         (pull["priority"], pull["param"], pull["window_s"], pull["tactic_mode"],
                          pull["cycle_spell"], pull["cycle_s"], pull["hp_min"], pull["hp_max"]),
                         "the Hakkar seek_pull row drifted")
        self.assertIsNone(new_kind_problem(pull))
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertEqual(1, len(re.findall(r"(?m)^\(309, 14834, '", text)))

    def test_hakkar_seek_pull_burns_on_arrival(self):
        # 20261108100000_world.sql: the 75 s kill lead forces the burn as soon as the pulled Son is engaged
        # (SeekLateEngagedBurnWithin), and the forced burn marks the gather ready, so the 30 % hold and the 8 s
        # gather wait drop out. The lead's ceiling: Poisonous Blood lasts 90 s, the siphon strips it only at its own
        # 8 s aura's end, and the 24320 cloud lasts 3 s, so 79 s is the most a Son killed now can still dose with.
        name = "20261108100000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the burn-on-arrival migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        pull = live.get((309, 14834, "seek_pull", 11357, 0))
        self.assertIsNotNone(pull, "the Hakkar seek_pull row is missing")
        self.assertEqual(75.0, pull["param"], "the kill lead is not 75 s")
        self.assertLessEqual(pull["param"], 90.0 - 8.0 - 3.0, "the kill lead outlives the poison's 79 s")
        self.assertEqual((3, 60.0), (pull["priority"], pull["window_s"]))
        self.assertIsNone(new_kind_problem(pull))
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertEqual(1, len(re.findall(r"(?m)^\(309, 14834, '", text)))

    def test_hakkar_anchor_rows(self):
        # 20261106030000_world.sql: the fight stays on his platform. The anchor centre is his spawn (tw_world creature
        # guid 49678, z 54.10), 30 yd for everyone and 25 yd for the tanks (priority); the script evades him below
        # z 45.8, which runs 45-50 yd SW of the spawn. The seek spot moves from 37 yd SW to 15 yd S of the spawn.
        name = "20261106030000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Hakkar anchor migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        anchor = live.get((309, 14834, "anchor", 0, 0))
        self.assertIsNotNone(anchor, "the Hakkar anchor row is missing")
        self.assertEqual(name, anchor["file"], "the anchor migration owns the row")
        self.assertEqual((25, 30.0, "", 0, 100, 0, 0, 0.0, -11787.4, -1649.34, 54.1),
                         (anchor["priority"], anchor["param"], anchor["tactic_mode"], anchor["hp_min"],
                          anchor["hp_max"], anchor["bot_aura"], anchor["cycle_spell"], anchor["cycle_s"],
                          anchor["spot_x"], anchor["spot_y"], anchor["spot_z"]),
                         "the Hakkar anchor row drifted")
        self.assertIsNone(new_kind_problem(anchor))
        anchors = [key for key in live if key[2] == "anchor"]
        self.assertEqual([(309, 14834, "anchor", 0, 0)], anchors, "only Hakkar is anchored")
        seek = live[(309, 14834, "seek_aura", 11357, 24321)]
        self.assertEqual(name, seek["file"], "the anchor migration re-inserts the seek_aura row")
        self.assertEqual((-11787.4, -1664.3, 54.0), (seek["spot_x"], seek["spot_y"], seek["spot_z"]))
        # The moved spot sits inside the anchor, 15 yd from the centre.
        dist = ((seek["spot_x"] - anchor["spot_x"]) ** 2 + (seek["spot_y"] - anchor["spot_y"]) ** 2) ** 0.5
        self.assertLess(dist, 16.0)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for kind, target, spell in (("anchor", 0, 0), ("seek_aura", 11357, 24321)):
            self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                          "AND `kind` = '%s' AND `target_entry` = %d AND `spell_id` = %d "
                          "AND `hp_min` = 0 AND `hp_max` = 100;" % (kind, target, spell), text)
        self.assertEqual(2, len(re.findall(r"(?m)^\(309, 14834, '", text)))
        self.assertNotIn("ALTER TABLE", text, "the anchor kind needs no new column")
        # The other Hakkar rows stay.
        for key in ((309, 14834, "pull_gate", 0, 24692), (309, 14834, "spread", 0, 24328),
                    (309, 14834, "threat_successor", 0, 0),
                    (309, 14834, "dps_hold_after", 0, 0)):
            self.assertIn(key, live)

    def test_anchor_row_shape_refused(self):
        anchor = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="anchor", priority=25, param=30.0,
                      spot_x=-11787.4, spot_y=-1649.34, spot_z=54.1)
        self.assertIsNone(new_kind_problem(anchor))
        for broken in (dict(anchor, spot_x=0.0, spot_y=0.0, spot_z=0.0), dict(anchor, param=5.0),
                       dict(anchor, param=80.0), dict(anchor, priority=40), dict(anchor, target_entry=11357),
                       dict(anchor, spell_id=24321), dict(anchor, hp_min=20), dict(anchor, tactic_mode="all"),
                       dict(anchor, cycle_spell=24324)):
            self.assertIsNotNone(new_kind_problem(broken), broken)

    def test_hakkar_opening_hold_migration(self):
        # 20261107120000_world.sql: D1 the 5 s opening threat hold. Its DELETE names that one key's full key, because
        # the row is new: no earlier migration owns it and the other Hakkar rows stay untouched.
        name = "20261107120000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Hakkar opening hold migration is missing")
        self.assertGreater(name, "20261105170000_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertNotIn("taunt_after_cast", text)
        self.assertEqual(1, text.count("INSERT INTO `ai_boss_tactic`"))
        self.assertEqual(1, len(re.findall(r"(?m)^\(309, 14834, '", text)))
        self.assertIn("(309, 14834, 'dps_hold_after', 0, 0, 0, 5, 'engage', 0, 0, 0, 0, 0, 0, ", text)

    def test_arlokk_rows(self):
        # 20261105190000_world.sql: the boss-first grid entries (Arlokk herself, then the Zulian Prowler while she is
        # vanished), the until_tank cover after an Ambush or a Gouge, and the 2 s hold on her panther transform. The
        # core ids are pinned in BossTacticsCoreData below; here the live table shape is, so a wrong number or a
        # delete that reaches too far fails.
        name = "20261105190000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Arlokk rows migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        boss = live.get((309, 14515, "kill_first", 14515, 0))
        self.assertIsNotNone(boss, "the Arlokk kill_first boss row is missing")
        self.assertEqual((1, 0.0, ""), (boss["priority"], boss["param"], boss["tactic_mode"]),
                         "the Arlokk kill_first boss row drifted")
        prowler = live.get((309, 14515, "kill_first", 15101, 0))
        self.assertIsNotNone(prowler, "the Arlokk kill_first prowler row is missing")
        self.assertEqual((2, 0.0, ""), (prowler["priority"], prowler["param"], prowler["tactic_mode"]),
                         "the Arlokk kill_first prowler row drifted")
        taunt = live.get((309, 14515, "taunt_after_cast", 0, 0))
        self.assertIsNotNone(taunt, "the Arlokk taunt_after_cast row is missing")
        self.assertEqual((1, 3.0, "until_tank"), (taunt["priority"], taunt["param"], taunt["tactic_mode"]),
                         "the Arlokk taunt_after_cast row drifted")
        hold = live.get((309, 14515, "dps_hold_after", 0, 24190))
        self.assertIsNotNone(hold, "the Arlokk dps_hold_after row is missing")
        self.assertEqual((0, 2.0, "until_tank"), (hold["priority"], hold["param"], hold["tactic_mode"]),
                         "the Arlokk dps_hold_after row drifted")
        for row in (boss, prowler, taunt, hold):
            self.assertIsNone(new_kind_problem(row))
        for key in ((309, 14515, "behind", 0, 26350), (309, 14515, "flee_on_aura", 0, 24210),
                    (309, 14515, "dispel_priority", 0, 24212)):
            self.assertIn(key, live, "the Arlokk rows of 20261028110000 and 20261103020000 must stay: %s" % (key,))
        if CORE and os.path.isfile(CORE_SPELLS):
            templates = {row["entry"] for row in
                         _core_rows("tw_world_creature_template.sql", "creature_template", ("entry",))}
            spells = {row["entry"] for row in _core_rows("tw_world_spell_template.sql", "spell_template", ("entry",))}
            for entry in (14515, 15101):
                self.assertIn(entry, templates, "Arlokk row entry %s has no creature_template row" % entry)
            for spell in (24190, 12540):
                self.assertIn(spell, spells, "Arlokk row spell %s has no spell_template row" % spell)

    def test_hakkar_taunt_after_cast_row_deleted(self):
        # 20261107200000_world.sql: Hakkar is taunt-immune (creature_template 14834 immunity_flags 6), so his
        # taunt_after_cast row (Cause Insanity 24327, 20261104020000) is deleted by its exact key with no
        # replacement; the threat_successor cap (20261105210000) already answers the charm reset. The file
        # itself stays as shipped and still loads on its own.
        name = "20261107200000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Hakkar taunt deletion migration is missing")
        self.assertGreater(name, "20261107120000_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertNotIn("INSERT INTO `ai_boss_tactic`", text)
        self.assertNotRegex(text, r"(?m)^(?:UPDATE|ALTER|DROP)\b")
        live = {(row[1], row[2], row[3], row[4], row[5]) for row in effective_rows()}
        self.assertNotIn((309, 14834, "taunt_after_cast", 0, 0), live)
        # The taunt-able ZG rows stay: the Berserker (11352) and Arlokk (14515).
        for boss in (11352, 14515):
            self.assertIn((309, boss, "taunt_after_cast", 0, 0), live)

    def test_seek_aura_row_without_cycle_spell_refused(self):
        seek = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="seek_aura", target_entry=11357,
                    spell_id=24321, priority=30, param=4.0, window_s=80.0, cycle_s=90.0,
                    spot_x=-11802.0, spot_y=-1683.0, spot_z=52.9)
        self.assertIsNotNone(new_kind_problem(seek))
        seek["cycle_spell"] = 24324
        self.assertIsNone(new_kind_problem(seek))

    def test_pull_gate_row_with_target_refused(self):
        gate = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="pull_gate", target_entry=11357,
                    spell_id=24692, param=30.0)
        self.assertIsNotNone(new_kind_problem(gate))
        gate["target_entry"] = 0
        self.assertIsNone(new_kind_problem(gate))

    def test_spread_row_param_below_five_refused(self):
        spread = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="spread", spell_id=24328, param=4.9)
        self.assertIsNotNone(new_kind_problem(spread))
        spread["param"] = 5.0
        self.assertIsNone(new_kind_problem(spread))

    def test_cycle_columns_on_other_kind_refused(self):
        interrupt = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="interrupt", spell_id=24324,
                         cycle_spell=24324)
        self.assertIsNotNone(new_kind_problem(interrupt))
        interrupt["cycle_spell"] = 0
        self.assertIsNone(new_kind_problem(interrupt))

    def test_gate_on_seek_aura_refused(self):
        seek = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="seek_aura", target_entry=11357,
                    spell_id=24321, priority=30, param=4.0, window_s=80.0, cycle_spell=24324,
                    cycle_s=90.0, spot_x=-11802.0, spot_y=-1683.0, spot_z=52.9, hp_max=50)
        self.assertIsNotNone(new_kind_problem(seek))

    def test_needs_aura_row_shape(self):
        # needs_aura (the Shade of Jin'do): the entry and the aura a bot must carry, and nothing else. The aura is the
        # row's gate, so a health band or a bot aura column, a mode word, or a missing half of the key is refused.
        shade = dict(INSERT_COLUMNS, map=309, boss_entry=11380, kind="needs_aura", target_entry=14986, spell_id=24306)
        self.assertIsNone(new_kind_problem(shade))
        self.assertIsNotNone(new_kind_problem(dict(shade, spell_id=0)), "needs_aura without its aura")
        self.assertIsNotNone(new_kind_problem(dict(shade, target_entry=0)), "needs_aura without its target")
        self.assertIsNotNone(new_kind_problem(dict(shade, bot_aura=24306)), "a bot aura on needs_aura")
        self.assertIsNotNone(new_kind_problem(dict(shade, hp_max=50)), "a health band on needs_aura")
        self.assertIsNotNone(new_kind_problem(dict(shade, tactic_mode="now")), "a mode word on needs_aura")

    def test_jindo_rows(self):
        # 20261105100000_world.sql: the off-tank cover after a Hex or a charm of the tank, and the Shade's aura gate.
        # The ids are pinned against the core below when TORTOISE_CORE is set; the boss's five older rows stay.
        name = "20261105100000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Jin'do rows migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        # The spell-less taunt_after_cast row of this file is deleted by 20261106120000: Jin'do is taunt-immune.
        self.assertNotIn((309, 11380, "taunt_after_cast", 0, 0), live)
        shade = live.get((309, 11380, "needs_aura", 14986, 24306))
        self.assertIsNotNone(shade, "the Jin'do needs_aura row is missing")
        self.assertEqual((0, 0.0, "", 0, 0),
                         (shade["priority"], shade["param"], shade["tactic_mode"], shade["hp_min"], shade["bot_aura"]),
                         "the Jin'do needs_aura row drifted")
        self.assertIsNone(new_kind_problem(shade))
        for key in ((309, 11380, "kill_first", 14986, 0), (309, 11380, "kill_first", 15112, 0),
                    (309, 11380, "kill_first", 14987, 0), (309, 11380, "dispel_priority", 0, 24306),
                    (309, 11380, "dispel_priority", 0, 17172)):
            self.assertIn(key, live, "the Jin'do rows of 20261103020000 must stay: %s" % (key,))
        if CORE and os.path.isfile(CORE_SPELLS):
            templates = {row["entry"] for row in
                         _core_rows("tw_world_creature_template.sql", "creature_template", ("entry",))}
            spells = {row["entry"] for row in _core_rows("tw_world_spell_template.sql", "spell_template", ("entry",))}
            for entry in (14986, 11380):
                self.assertIn(entry, templates, "Jin'do row entry %s has no creature_template row" % entry)
            for spell in (24306, 17172):
                self.assertIn(spell, spells, "Jin'do row spell %s has no spell_template row" % spell)

    def test_berserker_taunt_after_cast_row(self):
        # 20261107100200_world.sql: the Gurubashi Berserker (11352) resets its threat
        # every 10 s (Knock Away 11130) and every 25-30 s (Intimidating Roar 16508),
        # both through DoResetThreat (zulgurub_trash.cpp GurubashiBerserkerAI), so the
        # first off-tank takes it back within 5 s. Spell-less on purpose: the resets are
        # events, not a cast slot the boss side could key on; mode until_tank only.
        name = "20261107100200_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Berserker taunt_after_cast migration is missing")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        taunt = live.get((309, 11352, "taunt_after_cast", 0, 0))
        self.assertIsNotNone(taunt, "the Berserker taunt_after_cast row is missing")
        self.assertEqual((1, 5.0, "until_tank", 0, 100),
                         (taunt["priority"], taunt["param"], taunt["tactic_mode"], taunt["hp_min"], taunt["hp_max"]),
                         "the Berserker taunt_after_cast row drifted")
        self.assertIsNone(new_kind_problem(taunt))
    def test_berserker_row_is_documented(self):
        # docs/play/boss-tactics.md lists every live ZG taunt row with its
        # toggles; the Berserker row needs DungeonRunAuraTactics and
        # DungeonRunCharmYield. A spell-less until_tank row recurs on every
        # threat reset, so Known limits no longer demands a cast trigger.
        with open(DOC, encoding="utf-8") as handle:
            doc = handle.read()
        rows = [line for line in doc.splitlines() if "Gurubashi Berserker" in line]
        self.assertTrue(rows, "the Berserker row is missing from the ZG table")
        self.assertTrue(any("11352" in line for line in rows), "the Berserker row names no entry")
        self.assertTrue(any("DungeonRunCharmYield" in line for line in rows),
                        "the Berserker row names no charm-yield toggle")
        self.assertIn("spell-less `taunt_after_cast` mode `until_tank` covers a scripted threat reset", doc)

    def test_jindo_ward_before_totem_migration(self):
        # 20261105234000_world.sql: the Powerful Healing Ward 14987 heals Jin'do about as much as the raid deals, so it
        # outranks the Brain Wash Totem 15112. The migration deletes exactly those two rows by their key and inserts
        # them swapped; a delete that is too wide or a priority that drifted fails here.
        name = "20261105234000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Jin'do ward-first migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for entry in (15112, 14987):
            self.assertIn(
                "DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'kill_first' "
                "AND `target_entry` = %d AND `spell_id` = 0;" % entry, text)
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        ward = live.get((309, 11380, "kill_first", 14987, 0))
        totem = live.get((309, 11380, "kill_first", 15112, 0))
        self.assertIsNotNone(ward, "the Jin'do ward kill_first row is missing")
        self.assertIsNotNone(totem, "the Jin'do totem kill_first row is missing")
        self.assertEqual(1, ward["priority"], "the Powerful Healing Ward is not the first kill_first row")
        self.assertEqual(2, totem["priority"], "the Brain Wash Totem is not the second kill_first row")
        for row in (ward, totem):
            self.assertEqual(name, row["file"], "the ward-first migration no longer owns its row")
            self.assertIsNone(new_kind_problem(row))
        # The Shade's gated row (rank 0) still stands, and no other Jin'do row was touched.
        self.assertEqual(0, live[(309, 11380, "kill_first", 14986, 0)]["priority"])
        for key in ((309, 11380, "needs_aura", 14986, 24306), (309, 11380, "dispel_priority", 0, 24306),
                    (309, 11380, "dispel_priority", 0, 17172)):
            self.assertIn(key, live, "the Jin'do rows of 20261103020000 / 20261105100000 must stay: %s" % (key,))

    def test_jindo_banish_taunt_row(self):
        # 20261105233000_world.sql: the first off-tank takes Jin'do back from a tank Banish 24466 teleported onto the pit
        # trolls. The row is keyed to the spell (the glue reads the tank's landing at the spell's destination and a
        # jump of its position), sits beside the spell-less Hex/charm row of 20261105100000 and does not replace it.
        name = "20261105233000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the Jin'do Banish migration is missing")
        # Both Jin'do taunt rows are deleted again by 20261106120000 (he is taunt-immune); the file itself stays as
        # shipped and still loads on its own.
        first = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row)
                 for row in effective_rows([os.path.join(ROOT, "data/sql/world", n)
                                            for n in ("20261105100000_world.sql", name)])}
        banish = first.get((309, 11380, "taunt_after_cast", 0, 24466))
        self.assertIsNotNone(banish, "the Jin'do Banish taunt_after_cast row is missing from its own migration")
        self.assertEqual((1, 10.0, "until_tank", 0, 100, 0),
                         (banish["priority"], banish["param"], banish["tactic_mode"], banish["hp_min"],
                          banish["hp_max"], banish["bot_aura"]),
                         "the Jin'do Banish taunt_after_cast row drifted")
        self.assertIsNone(new_kind_problem(banish))
        live = {(row[1], row[2], row[3], row[4], row[5]) for row in effective_rows()}
        self.assertNotIn((309, 11380, "taunt_after_cast", 0, 24466), live)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertEqual(1, text.count("INSERT INTO `ai_boss_tactic`"), "one INSERT: the Banish row")
        self.assertIn("`spell_id` = 24466", text, "the DELETE must name the spell key")
        self.assertGreater(name, "20261105100000_world.sql")

    def test_the_stale_until_tank_rows_are_deleted_by_a_later_migration(self):
        # An earlier build of 20261103020000 shipped the two spell-less dps_hold_after until_tank rows (Hakkar's Cause
        # Insanity, Mandokir's Gaze). The AutoUpdater applies by file name, so a server that ran that build never sees
        # the corrected file: a later migration must delete exactly those two rows, and the corrected file must not
        # describe them as present any more.
        path = os.path.join(ROOT, "data/sql/world/20261103060000_world.sql")
        self.assertTrue(os.path.isfile(path), "the stale until_tank cleanup migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14834, 11382) "
                      "AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0;", text)
        self.assertNotIn("INSERT INTO `ai_boss_tactic`", text)
        self.assertGreater(os.path.basename(path), "20261103020000_world.sql")
        with open(os.path.join(ROOT, "data/sql/world/20261103020000_world.sql"), encoding="utf-8") as handle:
            aura = handle.read()
        self.assertNotIn("so DPS waits for the tank (until_tank, 4 s)", aura)
        self.assertNotIn("the tank must own the boss before DPS", aura)

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
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])): row for row in effective_rows()}
        key = (309, 14750, "avoid_cast", 0, 24024)
        self.assertIn(key, live)
        row = live[key]
        self.assertGreaterEqual(float(row[7]), 13.0)
        # F4-A: mode `all` clears the tanks too while the spell is cast; F4-B data: `priority` 50 is the
        # caster's own health % at or below which the zone is already live. The pinned values stop a
        # new migration from silently dropping either half.
        self.assertEqual("all", row[12], "the Bat Rider row lost mode `all`")
        self.assertEqual(50, int(row[6]), "the Bat Rider row lost the early-zone health (F4-B priority 50)")
        self.assertNotIn((309, 14750, "interrupt", 0, 24024),
                         {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()})

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

    def test_aq20_step_one_rows_are_live(self):
        # Walk-30 fixes, step 1 (20261104150000_world.sql): the rows of the kinds
        # that already have a consumer. burn_mana and ranged_only go in a later
        # migration with their C++, so the loader never skips them. Poison
        # Stinger 25748 has no row: preventionType 2, not SILENCE (1), so the
        # core cannot interrupt it (SpellEffects.cpp:3389-3393).
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        ossirian = live.get((509, 15339, "use_object", 180619, 25176))
        self.assertIsNotNone(ossirian, "the Ossirian crystal use_object row is missing")
        self.assertEqual(38.0, ossirian["window_s"], "the crystal window is not 38 s")
        self.assertEqual("tank_anchor", ossirian["tactic_mode"], "the crystal row lost tank_anchor")
        self.assertEqual(180619, ossirian["target_entry"])
        self.assertEqual(25176, ossirian["spell_id"])
        self.assertEqual((0, 25.0), (ossirian["priority"], ossirian["param"]))
        expected = {
            (509, 15348, "swap_on_aura", 0, 25646): (2, 5.0, "", 0, 100),
            (509, 15339, "taunt_after_cast", 0, 0): (1, 4.0, "until_tank", 0, 100),
            (509, 15339, "dispel_priority", 0, 25195): (1, 0.0, "now", 0, 100),
            (509, 15340, "kill_first", 15527, 0): (1, 0.0, "", 0, 100),
            (509, 15369, "kill_first", 15934, 0): (2, 0.0, "", 0, 100),
            # Ayamiss lands and wipes every player's threat at 70 % (boss_ayamiss.cpp:217-224). Taunt is melee range
            # (spell 355 rangeIndex 2), so the air phase above it must not spend the row: the band opens it only at
            # or below 70 %.
            (509, 15369, "taunt_after_cast", 0, 0): (0, 4.0, "until_tank", 0, 70),
            (509, 15324, "linked_death", 15324, 0): (1, 5.0, "", 0, 100),
        }
        for key, want in expected.items():
            self.assertIn(key, live, "missing AQ20 step-1 row %s" % (key,))
            row = live[key]
            self.assertEqual(want, (row["priority"], row["param"], row["tactic_mode"], row["hp_min"], row["hp_max"]),
                             "AQ20 row %s drifted" % (key,))
            self.assertIsNone(new_kind_problem(row))
        glad = live[(509, 15324, "linked_death", 15324, 0)]
        self.assertEqual(7.0, glad["window_s"])
        self.assertNotIn((509, 15369, "interrupt", 0, 25748), live)

    def test_aq20_burn_and_ranged_rows_are_live(self):
        # T2 burn_mana and T3 ranged_only (20261104150200_world.sql), written with their C++ so the loader never
        # skips them. Moam: up to four burners from 10 % mana. Ayamiss: melee held under an 8 yd gap while she is in
        # the air (she climbs 20 yd at 5 s, boss_ayamiss.cpp:193-205).
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        burn = live.get((509, 15340, "burn_mana", 0, 0))
        self.assertIsNotNone(burn, "the Moam burn_mana row is missing")
        self.assertEqual("20261104150200_world.sql", burn["file"], "the T2 migration owns the burn_mana row")
        self.assertEqual((4, 10.0, 0, 100), (burn["priority"], burn["param"], burn["hp_min"], burn["hp_max"]))
        self.assertIsNone(new_kind_problem(burn))
        hold = live.get((509, 15369, "ranged_only", 0, 0))
        self.assertIsNotNone(hold, "the Ayamiss ranged_only row is missing")
        self.assertEqual("20261104150200_world.sql", hold["file"], "the T3 migration owns the ranged_only row")
        self.assertEqual((0, 8.0, "", 0, 100),
                         (hold["priority"], hold["param"], hold["tactic_mode"], hold["hp_min"], hold["hp_max"]))
        self.assertIsNone(new_kind_problem(hold))
        # The earlier AQ20 rows stay untouched by the T2/T3 migration.
        for key in ((509, 15340, "kill_first", 15527, 0), (509, 15369, "kill_first", 15934, 0),
                    (509, 15369, "taunt_after_cast", 0, 0)):
            self.assertIn(key, live, "the T2/T3 migration removed %s" % (key,))

    def test_burn_mana_row_shape_refused(self):
        burn = dict(INSERT_COLUMNS, map=509, boss_entry=15340, kind="burn_mana", priority=4, param=10.0)
        self.assertIsNone(new_kind_problem(burn))
        for broken in (dict(burn, target_entry=15527), dict(burn, param=0.0), dict(burn, param=100.0),
                       dict(burn, priority=11), dict(burn, hp_max=50)):
            self.assertIsNotNone(new_kind_problem(broken), broken)

    def test_ranged_only_row_shape_refused(self):
        hold = dict(INSERT_COLUMNS, map=509, boss_entry=15369, kind="ranged_only", param=8.0)
        self.assertIsNone(new_kind_problem(hold))
        for broken in (dict(hold, param=2.0), dict(hold, param=41.0), dict(hold, hp_min=20)):
            self.assertIsNotNone(new_kind_problem(broken), broken)
        self.assertIsNone(new_kind_problem(dict(hold, target_entry=15934)))

    def test_gate_hold_row_shape_refused(self):
        gate = dict(INSERT_COLUMNS, map=249, boss_entry=10184, kind="gate_hold", target_entry=50143, param=42.0,
                    window_s=200.0)
        self.assertIsNone(new_kind_problem(gate))
        self.assertIsNone(new_kind_problem(dict(gate, window_s=0.0)))    # 0 = the 200 yd default
        self.assertIsNone(new_kind_problem(dict(gate, priority=2)))      # blockers that may still fight
        for broken in (dict(gate, target_entry=0), dict(gate, target_entry=gate["boss_entry"]),
                       dict(gate, spell_id=33439), dict(gate, param=0.0),
                       dict(gate, param=100.0), dict(gate, window_s=7.0), dict(gate, window_s=49.0),
                       dict(gate, window_s=251.0), dict(gate, hp_min=20), dict(gate, hp_max=50),
                       dict(gate, bot_aura=24306), dict(gate, tactic_mode="until_tank"), dict(gate, spot_x=1.0),
                       dict(gate, cycle_s=30.0)):
            self.assertIsNotNone(new_kind_problem(broken), broken)

    def test_hakkar_threat_successor_row_is_live(self):
        # 20261105210000: Hakkar alone asks for a threat successor, so his damage dealers stay below the assigned
        # off-tank and a Cause Insanity reset hands him to it. Every other taunt-immune boss keeps the highest tank.
        # 20261109120000: the row also carries his Cause Insanity timer (cycle_spell 24327, 19-26 s apart, 95 %),
        # read only with AiPlayerbot.DungeonRunThreatResetWindow.
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        cap = live.get((309, 14834, "threat_successor", 0, 0))
        self.assertIsNotNone(cap, "the Hakkar threat_successor row is missing")
        self.assertEqual("20261109120000_world.sql", cap["file"], "the reset window migration owns the row")
        self.assertEqual((95, 0, 19.0, 26.0, 24327, "", 0, 100, 0),
                         (cap["priority"], cap["spell_id"], cap["param"], cap["window_s"], cap["cycle_spell"],
                          cap["tactic_mode"], cap["hp_min"], cap["hp_max"], cap["bot_aura"]))
        self.assertIsNone(new_kind_problem(cap))
        # The new file writes one row and deletes the exact (map, boss, kind, target, spell, band) key.
        name = "20261109120000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the threat reset window migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertEqual(1, len(re.findall(r"(?m)^\(309, 14834, '", text)))
        # One row only: a second taunt-immune boss naming itself here would change its cap too.
        # Hakkar's and Jin'do's (20261106120000) only: another taunt-immune boss naming itself here would change its cap.
        successors = sorted(key for key in live if key[2] == "threat_successor")
        self.assertEqual([(309, 11380, "threat_successor", 0, 0), (309, 14834, "threat_successor", 0, 0)], successors)
        # The three earlier Hakkar rows stay (the taunt_after_cast row is gone: 20261107200000).
        for key in ((309, 14834, "seek_aura", 11357, 24321), (309, 14834, "pull_gate", 0, 24692),
                    (309, 14834, "spread", 0, 24328)):
            self.assertIn(key, live)

    def test_hakkar_reset_window_rollback_restores_the_plain_row(self):
        # Round-1 review: a binary from before 20261109120000 refuses a non-seek_aura row with a nonzero cycle_spell,
        # so on the migrated database it drops Hakkar's whole threat_successor row, not only the timer. The rollback
        # script (outside data/sql, so the updater never runs it) must leave the row exactly as 20261105210000 wrote it.
        # Replay every migration, then the rollback.
        rollback = os.path.join(ROOT, "tools/sql/rollback/20261109120000_world.rollback.sql")
        self.assertTrue(os.path.isfile(rollback), "the reset window rollback script is missing")
        self.assertNotIn(rollback, migration_paths())
        key = (309, 14834, "threat_successor", 0, 0)

        def row_of(paths):
            rows = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows(paths)}
            row = rows[key]
            row.pop("file")  # the rollback re-inserts, so its file name differs from the original insert's
            return row

        before = [path for path in migration_paths() if os.path.basename(path) < "20261109120000_world.sql"]
        plain = row_of(before)
        migrated = row_of(migration_paths())
        rolled = row_of(migration_paths() + [rollback])
        self.assertEqual(0, plain["cycle_spell"])
        self.assertEqual(24327, migrated["cycle_spell"])
        self.assertEqual(plain, rolled)
        self.assertEqual((0, 0.0, 0.0, 0, "", 0),
                         (rolled["priority"], rolled["param"], rolled["window_s"], rolled["cycle_spell"],
                          rolled["tactic_mode"], rolled["hp_min"]))
        # An older binary's loader (a non-seek_aura row takes no cycle_spell) accepts the rolled-back row again.
        self.assertIsNone(new_kind_problem(rolled))
        with open(rollback, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 "
                      "AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 "
                      "AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertEqual(1, len(re.findall(r"(?m)^\(309, 14834, '", text)))
        # The comment goes back to the one 20261105210000 inserted.
        old_comment = ("'Hakkar: Cause Insanity 24327 zeroes the main tank and taunts cannot land; the damage dealers "
                       "keep below the off-tank so a threat reset hands him to it'")
        with open(os.path.join(ROOT, "data/sql/world/20261105210000_world.sql"), encoding="utf-8") as handle:
            self.assertIn(old_comment + ");", handle.read())
        self.assertIn(old_comment + ");", text)

    def test_jindo_t1105h_migration(self):
        # 20261106120000: Jin'do is taunt-immune (creature_template 11380 immunity_flags 6), so his two taunt_after_cast
        # rows (Hex/charm, Banish) are deleted by their exact keys; the Sacrificed Trolls 14826 go to the damage dealers
        # (tank_ignore), and the damage dealers stay below the off-tank (threat_successor, Hakkar's shape).
        name = "20261106120000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the t1105h Jin'do migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for spell in (0, 24466):
            self.assertIn(
                "DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'taunt_after_cast' "
                "AND `target_entry` = 0 AND `spell_id` = %d AND `hp_min` = 0 AND `hp_max` = 100;" % spell, text)
        self.assertNotRegex(text, r"(?m)^(?:UPDATE|ALTER|DROP)\b")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        for spell in (0, 24466):
            self.assertNotIn((309, 11380, "taunt_after_cast", 0, spell), live)
        ignore = live.get((309, 11380, "tank_ignore", 14826, 0))
        self.assertIsNotNone(ignore, "the Jin'do tank_ignore row is missing")
        cap = live.get((309, 11380, "threat_successor", 0, 0))
        self.assertIsNotNone(cap, "the Jin'do threat_successor row is missing")
        for row in (ignore, cap):
            self.assertEqual(name, row["file"])
            self.assertEqual((0, 0.0, 0, 100, 0),
                             (row["priority"], row["param"], row["hp_min"], row["hp_max"], row["bot_aura"]))
        # The file inserted both rows plain; 20261108220000 later opts the threat_successor row into `strict`.
        self.assertEqual("", ignore["tactic_mode"])
        self.assertIn("(309, 11380, 'threat_successor', 0, 0, 0, 0, '', 0, 0, 0, 0, 0, 0, ", text)
        for row in (ignore, cap):
            self.assertIsNone(new_kind_problem(row))
        # The Shade's gate, its decurse ban and the kill_first rows stay.
        for key in ((309, 11380, "needs_aura", 14986, 24306), (309, 11380, "dispel_priority", 0, 24306),
                    (309, 11380, "kill_first", 14986, 0), (309, 11380, "kill_first", 14987, 0),
                    (309, 11380, "kill_first", 15112, 0), (309, 11380, "dispel_priority", 0, 17172)):
            self.assertIn(key, live)
        self.assertEqual("never", live[(309, 11380, "dispel_priority", 0, 24306)]["tactic_mode"])

    def test_tank_ignore_row_shape_refused(self):
        row = dict(INSERT_COLUMNS, map=309, boss_entry=11380, kind="tank_ignore", target_entry=14826)
        self.assertIsNone(new_kind_problem(row))
        for broken in (dict(row, target_entry=0), dict(row, spell_id=24306), dict(row, tactic_mode="until_tank"),
                       dict(row, hp_min=20), dict(row, bot_aura=24306), dict(row, spot_x=1.0)):
            self.assertIsNotNone(new_kind_problem(broken), broken)

    def test_strict_opt_in_rollback_restores_the_plain_row(self):
        # Codex r5: a binary from before `strict` refuses Jin'do's migrated row and loses his whole successor cap. The
        # rollback script (outside data/sql, so the updater never runs it) must leave the row exactly as 20261106120000
        # wrote it. Replay every migration, then the rollback.
        rollback = os.path.join(ROOT, "tools/sql/rollback/20261108220000_world.rollback.sql")
        self.assertTrue(os.path.isfile(rollback), "the strict opt-in rollback script is missing")
        self.assertNotIn(rollback, migration_paths())
        key = (309, 11380, "threat_successor", 0, 0)

        def live(paths):
            return {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows(paths)}

        before = [path for path in migration_paths() if os.path.basename(path) < "20261108220000_world.sql"]
        plain = live(before)[key]
        migrated = live(migration_paths())[key]
        rolled = live(migration_paths() + [rollback])[key]
        self.assertEqual("", plain["tactic_mode"])
        self.assertEqual("strict", migrated["tactic_mode"])
        self.assertEqual("", rolled["tactic_mode"])
        self.assertEqual(plain, rolled)
        # An older binary's loader (a threat_successor row takes no mode word) accepts the rolled-back row again.
        self.assertEqual("", rolled["tactic_mode"])
        self.assertIsNone(new_kind_problem(rolled))
        self.assertEqual("", live(migration_paths() + [rollback])[(309, 14834, "threat_successor", 0, 0)]["tactic_mode"])
        with open(rollback, encoding="utf-8") as handle:
            text = handle.read()
        self.assertNotRegex(text, r"(?m)^(?:INSERT|DELETE|ALTER|DROP)\b")
        # The comment goes back to the one 20261106120000 inserted (the replay does not track comments).
        comment = ("'Jindo: taunt-immune (immunity_flags 6) and Hex wipes the tank threat; the damage dealers keep below "
                   "the off-tank so it inherits him'")
        with open(os.path.join(ROOT, "data/sql/world/20261106120000_world.sql"), encoding="utf-8") as handle:
            self.assertIn(comment + ");", handle.read())
        self.assertIn("`comment` = " + comment + "\n", text)

    def test_threat_successor_row_shape_refused(self):
        cap = dict(INSERT_COLUMNS, map=309, boss_entry=14834, kind="threat_successor")
        self.assertIsNone(new_kind_problem(cap))
        self.assertIsNone(new_kind_problem(dict(cap, tactic_mode="strict")))
        for broken in (dict(cap, target_entry=11357), dict(cap, tactic_mode="until_tank"), dict(cap, hp_min=20),
                       dict(cap, bot_aura=24321), dict(cap, tactic_mode="all")):
            self.assertIsNotNone(new_kind_problem(broken), broken)

    def test_strict_successor_opt_in_is_jindo_only(self):
        # 20261108220000 (t1106m): AiPlayerbot.DungeonRunThreatSuccessorStrict needs the row's `strict` word. Jin'do's
        # threat_successor row carries it; Hakkar's stays plain (walk 57: 0/58 kills on Hakkar, against 4/41).
        name = "20261108220000_world.sql"
        path = os.path.join(ROOT, "data/sql/world", name)
        self.assertTrue(os.path.isfile(path), "the strict opt-in migration is missing")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("UPDATE `ai_boss_tactic` SET `tactic_mode` = 'strict',", text)
        self.assertIn("WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'threat_successor' AND `target_entry` = 0 "
                      "AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;", text)
        self.assertNotRegex(text, r"(?m)^(?:INSERT|DELETE|ALTER|DROP)\b")
        self.assertNotIn("14834", text.split("\n\n", 1)[1] if "\n\n" in text else "")
        live = {(row[1], row[2], row[3], row[4], row[5]): _payload_dict(row) for row in effective_rows()}
        jindo = live[(309, 11380, "threat_successor", 0, 0)]
        hakkar = live[(309, 14834, "threat_successor", 0, 0)]
        self.assertEqual("strict", jindo["tactic_mode"])
        self.assertEqual("", hakkar["tactic_mode"])
        self.assertIsNone(new_kind_problem(jindo))
        # Only Jin'do opts in.
        self.assertEqual([(309, 11380, "threat_successor", 0, 0)],
                         sorted(key for key, row in live.items() if key[2] == "threat_successor"
                                and row["tactic_mode"] == "strict"))

    def test_aq20_step_one_keeps_the_ayamiss_larva_row(self):
        # 20261104150000 re-inserts Ayamiss's hornet row 15934 (kill_first). Its DELETE used to match every kill_first
        # of boss 15369, so it also removed the larva row 15555 from 20261028120000 and bots stopped killing larvae
        # first. Each DELETE of this file may remove only keys this file inserts; re-applying the file is a no-op.
        first = os.path.join(ROOT, "data/sql/world/20261028120000_world.sql")
        step = os.path.join(ROOT, "data/sql/world/20261104150000_world.sql")

        def key(row):
            return (row[1], row[2], row[3], row[4], row[5], row[9], row[10])

        before = {key(row) for row in effective_rows([first])}
        self.assertIn((509, 15369, "kill_first", 15555, 0, 0, 100), before, "the larva row is not in 20261028120000")
        own = {key(payload) for kind, payload in migration_statements([step]) if kind == "insert"}
        for kind, payload in migration_statements([step]):
            if kind != "delete":
                continue
            _name, match = payload
            scope = delete_scope(match)
            removed = {k for k in before if delete_covers(scope, k)}
            self.assertLessEqual(removed, own,
                                 "the DELETE `%s` removes rows this file does not insert"
                                 % match.string.strip())

        once = effective_rows([first, step])
        live = {key(row) for row in once}
        self.assertIn((509, 15369, "kill_first", 15555, 0, 0, 100), live, "the larva row is gone")
        self.assertIn((509, 15369, "kill_first", 15934, 0, 0, 100), live, "the hornet row is gone")
        twice = effective_rows([first, step, step])
        self.assertEqual(set(once), set(twice), "re-applying 20261104150000 changes the live rows")


    def test_migrations_remove_only_rows_they_replace(self):
        # Every data/sql/world migration's DELETE on ai_boss_tactic removes only rows the same file re-inserts.
        # A re-export sorted after an earlier file (the old-style full-boss plan DELETE) would silently drop a
        # live row it never replaces; a fix that removes a bad row names its replacement, or no replacement when
        # the core cannot use it. Apply all migrations in order on an empty schema and fail on a row an earlier
        # file inserted that disappears without the same file inserting its key.
        # Intent removals the core audit asked for (a kick the core cannot stop, so no replacement exists):
        #   map 822 boss 63133 spell 3143, map 820 boss 62771 spell 36732 (20261020082925);
        #   map 70 boss 4854 spell 8292 (20261029150000);
        #   map 309 boss 14507 spell 23895, boss 14834 spell 24324, map 409 boss 12119 spell 20604,
        #   map 531 bosses 15275/15276 spell 7393, map 532 boss 61224 spell 57094 (20261101234600).
        # Renew 23895's replacement (Holy Fire 23860) lives one file later (20261101080000).
        # Jin'do's two taunt_after_cast rows (spell 0 and Banish 24466): he is taunt-immune, so no taunt row can work
        # (20261106120000, t1105h). Hakkar's taunt_after_cast row (spell 0): the same immunity, so the dead row is
        # deleted with no replacement (20261107200000).
        allowed = {
            (309, 11380, "taunt_after_cast", 0, 0, 0, 100),
            (309, 11380, "taunt_after_cast", 0, 24466, 0, 100),
            (309, 14834, "taunt_after_cast", 0, 0, 0, 100),
            (822, 63133, "interrupt", 0, 3143, 0, 100),
            (820, 62782, "interrupt", 62771, 36732, 0, 100),
            (70, 4854, "interrupt", 0, 8292, 0, 100),
            (309, 14507, "interrupt", 0, 23895, 0, 100),
            (309, 14834, "interrupt", 0, 24324, 0, 100),
            (409, 12118, "interrupt", 12119, 20604, 0, 100),
            (531, 15275, "interrupt", 0, 7393, 0, 100),
            (531, 15276, "interrupt", 0, 7393, 0, 100),
            (532, 61224, "interrupt", 0, 57094, 0, 100),
        }

        def key(row):
            return (row[1], row[2], row[3], row[4], row[5], row[9], row[10])

        live = {}
        for path in migration_paths():
            name = os.path.basename(path)
            own = {key(payload) for kind, payload in migration_statements([path]) if kind == "insert"}
            for kind, payload in migration_statements([path]):
                if kind != "delete":
                    continue
                _name, match = payload
                scope = delete_scope(match)
                removed = {live_key for live_key in live if delete_covers(scope, live_key)}
                dropped = {live_key for live_key in removed - own if live_key not in allowed}
                self.assertFalse(dropped, "%s DELETE `%s` removes live rows it does not re-insert: %s"
                                 % (name, match.string.strip(), sorted(dropped)))
                for live_key in removed:
                    del live[live_key]
            for own_key in own:
                live[own_key] = name

    def test_stand_migrations_remove_only_rows_they_replace(self):
        # Same rule as above for ai_boss_stand, keyed by its primary key
        # (map, boss_entry, anchor, role, window_spell, side, seq). Only
        # 20261101080000 writes the table so far; its DELETE names the same
        # (309, 14507) boss it inserts, so a future re-export cannot silently
        # drop another boss's seats.
        stand_delete = re.compile(
            r"^DELETE FROM `ai_boss_stand` WHERE `map` = (\d+)"
            r"(?: AND `boss_entry` = (\d+))?;?$"
        )

        def stand_key(columns, tuple_text):
            values = _selected_values(tuple_text, set(range(len(columns))))
            row = dict(zip(columns, (values[i] for i in range(len(columns)))))
            return (int(row["map"]), int(row["boss_entry"]), int(row["anchor"]),
                    str(row["role"]).strip("'"), int(row["window_spell"]),
                    int(row["side"]), int(row["seq"]))

        live = {}
        for path in migration_paths():
            name = os.path.basename(path)
            with open(path, encoding="utf-8") as handle:
                text = handle.read()
            if "`ai_boss_stand`" not in text:
                continue
            own = set()
            deletes = []
            for statement in _sql_statements(text):
                line = statement.strip()
                if line.startswith("INSERT INTO `ai_boss_stand`"):
                    columns = [c.strip().strip("`") for c in line.split("(", 1)[1].split(")", 1)[0].split(",")]
                    values = line.split("VALUES", 1)[1]
                    for tuple_text in _tuples(values):
                        own.add(stand_key(columns, tuple_text))
                elif line.startswith("DELETE FROM `ai_boss_stand`"):
                    match = stand_delete.match(line)
                    self.assertIsNotNone(match, "unparsable stand DELETE in %s: %s" % (name, line))
                    deletes.append((int(match.group(1)), int(match.group(2)) if match.group(2) else None))
            for map_id, boss in deletes:
                removed = {stand_row for stand_row in live
                           if stand_row[0] == map_id and (boss is None or stand_row[1] == boss)}
                dropped = removed - own
                self.assertFalse(dropped, "%s stand DELETE (map %s boss %s) removes seats it does not re-insert: %s"
                                 % (name, map_id, boss, sorted(dropped)))
                for stand_row in removed:
                    del live[stand_row]
            for own_row in own:
                live[own_row] = name


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

_CORE_ROWS = {}


def _core_rows(filename, table, wanted):
    """One core table, read once per process: the row-level tests outside BossTacticsCoreData share the parse."""
    key = (filename, table, tuple(wanted))
    if key not in _CORE_ROWS:
        _CORE_ROWS[key] = _read_core_table(CORE, filename, table, wanted)
    return _CORE_ROWS[key]


@unittest.skipUnless(CORE and os.path.isfile(CORE_SPELLS), "set TORTOISE_CORE=<tortoise-wow checkout>")
class BossTacticsCoreData(unittest.TestCase):
    SPELL_COLUMNS = ("entry", "attributesEx", "castingTimeIndex", "interruptFlags",
                     "channelInterruptFlags", "preventionType", "dispel", "stackAmount",
                     "effect1", "effect2", "effect3", "effectTriggerSpell1", "effectTriggerSpell2",
                     "effectTriggerSpell3", "effectImplicitTargetA1", "effectImplicitTargetA2",
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
        50144: "src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp:720-721 "
               "(Onyxia summons two Onyxian Hatchers at the 40 % landing)",
    }
    # Entries whose template row lives in sql/database_updates/world rather than the base dumps
    # (creature_template 20260510092659_world.sql). 50143's row is NOT waived: the core update file is parsed and
    # the spawn row asserted below. Only the Hatcher 50144 is excepted: it is summoned, so it has no spawn row.
    CORE_UPDATE_ENTRIES = {
        50144: "sql/database_updates/world/20260510092659_world.sql (template; no spawn row: summoned, see SUMMONED)",
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
        # Every spell a new-kind row names exists in spell_template: a dispel_priority aura, a freeze/flee aura, the
        # aura a dps_hold_after row triggers on, a swap_on_aura stack source or a kill_first target's spell id (0 for
        # the target rows).
        aura_kinds = ("dispel_priority", "freeze_on_aura", "flee_on_aura", "dps_hold_after", "swap_on_aura")
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

    def test_seek_aura_rows_name_a_source_cloud(self):
        # A seek_aura row's aura has to come out of the source's death: the trigger spell of an effect-27 persistent
        # area aura cast by one of the target's EventAI death scripts (the Son of Hakkar 11357's event 1135701 runs
        # script 1135701, which casts 24320, effect 27, trigger 24321). The 24320 branch of SpellEffects is dead code,
        # so this EventAI path is the only source of the cloud.
        events = _read_core_table(CORE, "tw_world_creature_ai_events.sql", "creature_ai_events",
                                  ("id", "creature_id", "event_type", "action1_script", "action2_script",
                                   "action3_script"))
        scripts = _read_core_table(CORE, "tw_world_creature_ai_scripts.sql", "creature_ai_scripts",
                                   ("id", "command", "datalong"))
        problems = []
        for row in effective_rows():
            if row[3] != "seek_aura":
                continue
            where = "map %s boss %s source %s aura %s" % (row[1], row[2], row[4], row[5])
            script_ids = set()
            for event in events:
                if event.get("creature_id") != int(row[4]) or event.get("event_type") != 6:
                    continue
                for index in (1, 2, 3):
                    if event.get("action%d_script" % index):
                        script_ids.add(event["action%d_script" % index])
            found = False
            for script in scripts:
                if script.get("id") not in script_ids or script.get("command") != 15:
                    continue
                info = self.spells.get(script.get("datalong"))
                if not info:
                    continue
                for index in (1, 2, 3):
                    if (info.get("effect%d" % index) == 27 and
                            info.get("effectTriggerSpell%d" % index) == int(row[5])):
                        found = True
                        break
            if not found:
                problems.append("%s: no death script of the source casts an effect-27 spell triggering the aura"
                                % where)
        self.assertEqual([], problems, "seek_aura rows the source cannot produce:\n" + "\n".join(problems))

    def test_new_kind_spells_exist(self):
        # The seek_aura clock's spell is a core spell, and the pull gate's stake spell carries more than one stack: a
        # single-stack aura could never sit above the band a row allows.
        problems = []
        for row in effective_rows():
            where = "map %s boss %s kind %s" % (row[1], row[2], row[3])
            if row[3] == "seek_aura" and row[14] not in self.spells:
                problems.append("%s: cycle_spell %s has no spell_template row" % (where, row[14]))
            if row[3] == "pull_gate":
                info = self.spells.get(int(row[5]))
                if info is None:
                    problems.append("%s: spell %s has no spell_template row" % (where, row[5]))
                elif info["stackAmount"] <= 1:
                    problems.append("%s: spell %s has stackAmount %s, so the gate could never bind"
                                    % (where, row[5], info["stackAmount"]))
        self.assertEqual([], problems, "new kind spells the core does not answer:\n" + "\n".join(problems))

    def test_boss_and_target_entries_exist(self):
        for row in effective_rows():
            where = "map %s boss %s kind %s" % (row[1], row[2], row[3])
            self.assertIn(int(row[2]), self.templates, "%s: boss_entry has no creature_template row" % where)
            if int(row[4]) and row[3] != "use_object":
                self.assertIn(int(row[4]), self.templates, "%s: target_entry %s has no creature_template row" % (where, row[4]))

    def test_bosses_spawn_on_their_map(self):
        for row in effective_rows():
            map_id, boss = int(row[1]), int(row[2])
            if boss in self.SUMMONED or boss in self.CORE_UPDATE_ENTRIES:
                continue
            self.assertIn((map_id, boss), self.spawns,
                          "map %s boss %s has no creature row with its entry on that map (and no SUMMONED reason)" % (map_id, boss))
    def test_cindarion_spawns_on_map_249(self):
        # Cindarion 50143 has no SUMMONED entry, so test_bosses_spawn_on_their_map requires a
        # (249, 50143) row in the base creature dump: the core ships it in a database update instead,
        # so this test parses that update and proves the spawn row itself.
        path = os.path.join(CORE, "sql", "database_updates", "world", "20261108142635_world.sql")
        self.assertTrue(os.path.isfile(path), "Cindarion's spawn update is gone: %s" % path)
        with open(path, encoding="utf-8", errors="replace") as handle:
            update_text = handle.read()
        base_path = os.path.join(CORE, "sql", "base", "tw_world_creature.sql")
        with open(base_path, encoding="utf-8", errors="replace") as handle:
            base_text = handle.read()
        fields = _create_table_fields(base_text, "creature", base_path)
        rows = _apply_dump(update_text, "creature", fields, ("guid", "id", "map"))
        self.assertIn({"guid": 473281, "id": 50143, "map": 249}, rows,
                      "no creature row with entry 50143 on map 249 (guid 473281) in %s" % path)

    def test_core_update_entries_are_pinned(self):
        # The CORE_UPDATE_ENTRIES waiver cannot go stale: every named file exists under TORTOISE_CORE and
        # contains the entry.
        for entry, reason in self.CORE_UPDATE_ENTRIES.items():
            files = [token for token in reason.split() if token.startswith("sql/")]
            self.assertTrue(files, "entry %s: no file in %r" % (entry, reason))
            for name in files:
                path = os.path.join(CORE, name.rstrip(",;"))
                self.assertTrue(os.path.isfile(path), "entry %s: %s is gone" % (entry, path))
                with open(path, encoding="utf-8", errors="replace") as handle:
                    self.assertIn(str(entry), handle.read(), "entry %s: %s no longer names it" % (entry, path))

    def test_spell_table_updates_do_not_touch_the_rule(self):
        self.assertEqual(
            [],
            self.rule_updates,
            "database_updates/world UPDATEs spell_template columns the interrupt rule reads; extend the reader",
        )


if __name__ == "__main__":
    unittest.main()
