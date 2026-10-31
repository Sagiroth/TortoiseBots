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
KINDS = {"interrupt", "kill_first", "behind", "avoid_cast"}
ROW = re.compile(r"^\((\d+), (\d+), '(\w+)', (\d+), (\d+), (\d+), (-?[\d.]+), '(?:[^']|'')*'\)[,;]$")
DELETE = re.compile(
    r"^DELETE FROM `ai_boss_tactic` WHERE `map` = (\d+)"
    r"(?: AND `boss_entry` IN \(([\d, ]+)\)| AND `boss_entry` = (\d+))?"
    r"(?: AND `kind` = '(\w+)' AND `target_entry` = (\d+) AND `spell_id` = (\d+))?;?$"
)
DELETE_LINE = re.compile(r"DELETE\s+FROM\s+`?ai_boss_tactic`?(?=\s|;|$)", re.IGNORECASE)


def migration_paths():
    return sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql")))


def migration_statements():
    """Yield ("insert", row) and ("delete", (name, match)) in file and line order."""
    for path in migration_paths():
        name = os.path.basename(path)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_boss_tactic`" not in text:
            continue
        in_insert = False
        for line in text.splitlines():
            if line.startswith("INSERT INTO `ai_boss_tactic`"):
                in_insert = True
                continue
            if DELETE_LINE.match(line):
                in_insert = False
                match = DELETE.match(line)
                if not match:
                    raise AssertionError("unparsable DELETE in %s: %s" % (name, line))
                yield "delete", (name, match)
                continue
            if not in_insert or line.startswith("--") or not line.strip():
                continue
            match = ROW.match(line)
            if not match:
                raise AssertionError("unparsable ai_boss_tactic row in %s: %s" % (name, line))
            yield "insert", (name,) + match.groups()
            if line.endswith(";"):
                in_insert = False


def migration_rows():
    return [row for kind, row in migration_statements() if kind == "insert"]


def effective_rows():
    """The rows a boot ends up with: every INSERT, minus the DELETEs that follow it."""
    live = {}
    for kind, payload in migration_statements():
        if kind == "insert":
            live[(int(payload[1]), int(payload[2]), payload[3], int(payload[4]), int(payload[5]))] = payload
            continue
        _name, match = payload
        map_id = int(match.group(1))
        if match.group(4):
            live.pop((map_id, int(match.group(3)), match.group(4), int(match.group(5)), int(match.group(6))), None)
        elif match.group(2):
            bosses = {int(entry) for entry in match.group(2).split(",")}
            for key in [key for key in live if key[0] == map_id and key[1] in bosses]:
                del live[key]
        else:
            for key in [key for key in live if key[0] == map_id]:
                del live[key]
    return list(live.values())


class BossTacticsSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_tactic rows found")

    def test_rows_are_loadable(self):
        for name, map_id, boss, kind, target, spell, priority, param in effective_rows():
            where = "%s boss %s kind %s" % (name, boss, kind)
            self.assertIn(kind, KINDS, where)
            self.assertGreater(int(map_id), 0, where)
            self.assertGreater(int(boss), 0, where)
            self.assertLess(int(priority), 256, where)
            if kind == "interrupt":
                self.assertGreater(int(spell), 0, where)
            if kind == "behind":
                self.assertGreater(float(param), 0.0, where)
            if kind == "avoid_cast":
                self.assertGreater(int(spell), 0, where)
                self.assertGreater(float(param), 0.0, where)

    def test_primary_keys_are_unique(self):
        seen = set()
        for name, map_id, boss, kind, target, spell, _priority, _param in migration_rows():
            key = (map_id, boss, kind, target, spell)
            self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, name))
            seen.add(key)

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

    def test_deleted_rows_are_gone(self):
        # pjerra/turtle-agent#246: the rows below are not interruptible under the
        # core's rule (preventionType 0, or interruptFlags without the damage bit)
        # and were deleted by a later migration.
        live = {(int(row[1]), int(row[2]), row[3], int(row[4]), int(row[5])) for row in effective_rows()}
        self.assertNotIn((822, 63133, "interrupt", 0, 3143), live)
        self.assertNotIn((820, 62782, "interrupt", 62771, 36732), live)
        self.assertNotIn((70, 4854, "interrupt", 0, 8292), live)

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
                     "channelInterruptFlags", "preventionType")
    # Creatures the core puts on a map without a creature row: the summon that makes them.
    SUMMONED = {
        3654: "src/scripts/dungeons/wailing_caverns/wailing_caverns.cpp:504 "
              "(SummonAttacker in the Disciple of Naralex event)",
        7273: "sql/base/tw_world_event_scripts.sql:58, event 2488 (the eventId of the "
              "Gong of Zul'Farrak 141832, tw_world_gameobject_template.sql:72) summons him",
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

    def test_avoid_cast_spells_are_casts(self):
        # The avoid_cast scan reads the caster's current generic spell, which an instant
        # spell never stays in long enough to be seen: a cast time is required.
        rows = sorted((int(row[1]), int(row[2]), int(row[5])) for row in effective_rows() if row[3] == "avoid_cast")
        problems = []
        for map_id, boss, spell in rows:
            info = self.spells.get(spell)
            if info is None:
                problems.append("map %s boss %s spell %s: no spell_template row" % (map_id, boss, spell))
            elif info["castingTimeIndex"] <= 1:
                problems.append("map %s boss %s spell %s: castingTimeIndex %s, the cast is instant"
                                % (map_id, boss, spell, info["castingTimeIndex"]))
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
