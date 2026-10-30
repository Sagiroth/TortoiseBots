#!/usr/bin/env python3
"""Boss coverage: which boss abilities the bots have no answer for.

The rule is in specs/fd-tactics-coverage.md of the audit repo: every ability and
every add line carries needs (interrupt, kill, front, tail, avoid, mark, dispel,
purge, resist); a need is answered by a tactic row, a stand set or a code answer
listed in data/boss/code_answers.csv. `python3 tools/boss_data.py coverage`
prints the gaps, boss by boss; the dashboard runs the same rule in
web/static/js/coverage.js, and data/boss/testdata/coverage_cases.json keeps the
two implementations equal.

Rows come from replaying data/sql/world/*.sql (`INSERT`/`REPLACE INTO`, the
scoped `DELETE`s and the `UPDATE ... SET ... WHERE ...` statements of
ai_boss_tactic, ai_boss_stand and ai_dungeon_plan) or, with --db, from the
world DB through boss_data.db_connect. A row statement touching one of those
tables that the replay cannot apply fails the run instead of being skipped. The
table is built in memory from the CSV, so an uncommitted CSV edit shows up at
once.

No boss entry, spell id, raid key or boss name is written in this module: every
boss fact is a row of the CSV, the migrations or the code-answer file.
"""

import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

import boss_data

COUNT_KEYS = ("answered", "partly", "missing", "notneeded", "unknown")
NEED_ORDER = (
    "interrupt",
    "kill",
    "front",
    "tail",
    "avoid",
    "mark",
    "dispel",
    "purge",
    "resist",
    "classify",
)
# A need a world table row (tactic row, stand set, plan list) could answer.
DB_NEEDS = frozenset(("interrupt", "kill", "front", "tail", "avoid", "mark"))
LEVEL_ORDER = {"none": 0, "unknown": 1, "partly": 2, "full": 3}

QUERY_TACTICS = (
    "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment` "
    "FROM `ai_boss_tactic` ORDER BY `map`, `boss_entry`, `kind`, `target_entry`, `spell_id` LIMIT 5000"
)
QUERY_STANDS = (
    "SELECT `map`, `boss_entry`, `window_spell`, `aura`, `role`, `window_signal`, SUM(`slots`) "
    "FROM `ai_boss_stand` GROUP BY `map`, `boss_entry`, `window_spell`, `aura`, `role`, `window_signal` "
    "ORDER BY `map`, `boss_entry`, `window_spell`, `role` LIMIT 5000"
)
QUERY_PLAN_KILL_FIRST = (
    "SELECT `plan`, `map`, `kill_first` FROM `ai_dungeon_plan` ORDER BY `plan` LIMIT 1000"
)

QUERY_STEPS = (
    "SELECT s.`plan`, p.`map`, s.`kind`, s.`entry`, s.`entries` "
    "FROM `ai_dungeon_step` s JOIN `ai_dungeon_plan` p ON p.`plan` = s.`plan` "
    "ORDER BY s.`plan`, s.`leg`, s.`seq` LIMIT 20000"
)

SQL_TABLES = ("ai_boss_tactic", "ai_boss_stand", "ai_dungeon_plan", "ai_dungeon_step")

# SQL column -> row field, per table.
TACTIC_FIELDS = {
    "map": ("map", int),
    "boss_entry": ("boss", int),
    "kind": ("kind", str),
    "target_entry": ("target", int),
    "spell_id": ("spell", int),
    "priority": ("priority", int),
    "param": ("param", float),
    "comment": ("comment", str),
}
STAND_FIELDS = {
    "map": ("map", int),
    "boss_entry": ("boss", int),
    "window_spell": ("windowSpell", int),
    "aura": ("aura", int),
    "role": ("role", str),
    "window_signal": ("signal", str),
    "slots": ("slots", int),
}
STAND_DEFAULTS = {"windowSpell": 0, "aura": 0, "signal": "cast", "slots": 5}
PLAN_FIELDS = {"plan": ("plan", str), "map": ("map", int), "kill_first": ("killFirst", str)}
PLAN_DEFAULTS = {"killFirst": ""}
# ai_dungeon_step: only the columns the boss steps' kill lists need (kind boss: entry is the boss, entries its adds).
STEP_FIELDS = {
    "plan": ("plan", str),
    "leg": ("leg", int),
    "seq": ("seq", int),
    "kind": ("kind", str),
    "entry": ("entry", int),
    "entries": ("entries", str),
    "x": ("x", float),
    "y": ("y", float),
    "z": ("z", float),
}
STEP_DEFAULTS = {"kind": "", "entry": 0, "entries": "", "x": 0.0, "y": 0.0, "z": 0.0}
ABS_RE = re.compile(r"ABS\(\s*`(\w+)`\s*-\s*(-?\d+(?:\.\d+)?)\s*\)\s*<\s*(\d+(?:\.\d+)?)", re.I)

INSERT_RE = re.compile(
    r"^\s*(?:INSERT|REPLACE)\s+INTO\s+`(\w+)`\s*\(([^)]*)\)\s*VALUES\s*(.+)$", re.I | re.S
)
DELETE_RE = re.compile(r"^\s*DELETE\s+FROM\s+`(\w+)`\s+WHERE\s+(.+)$", re.I | re.S)
UPDATE_RE = re.compile(
    r"^\s*UPDATE\s+`(\w+)`\s+SET\s+(.+?)\s+WHERE\s+(.+)$", re.I | re.S
)
SCHEMA_KEYWORDS = frozenset(("CREATE", "ALTER", "SET"))


# --------------------------------------------------------------------------
# SQL replay


def _strip_comments(text):
    """Drop `-- ...` comments, leaving quoted strings intact."""
    out = []
    i = 0
    length = len(text)
    quote = False
    while i < length:
        char = text[i]
        if quote:
            out.append(char)
            if char == "'":
                if i + 1 < length and text[i + 1] == "'":
                    out.append("'")
                    i += 2
                    continue
                quote = False
            elif char == "\\" and i + 1 < length:
                out.append(text[i + 1])
                i += 2
                continue
            i += 1
        elif char == "'":
            quote = True
            out.append(char)
            i += 1
        elif char == "-" and text[i : i + 2] == "--":
            end = text.find("\n", i)
            if end < 0:
                break
            i = end
        else:
            out.append(char)
            i += 1
    return "".join(out)


def _split_statements(text):
    statements = []
    buf = []
    i = 0
    length = len(text)
    quote = False
    while i < length:
        char = text[i]
        if quote:
            buf.append(char)
            if char == "'":
                if i + 1 < length and text[i + 1] == "'":
                    buf.append("'")
                    i += 2
                    continue
                quote = False
            elif char == "\\" and i + 1 < length:
                buf.append(text[i + 1])
                i += 2
                continue
            i += 1
        elif char == "'":
            quote = True
            buf.append(char)
            i += 1
        elif char == ";":
            statement = "".join(buf).strip()
            if statement:
                statements.append(statement)
            buf = []
            i += 1
        else:
            buf.append(char)
            i += 1
    tail = "".join(buf).strip()
    if tail:
        statements.append(tail)
    return statements


def _parse_values(text, where):
    """The `VALUES (...), (...)` list as Python scalars; anything else fails."""
    values = []
    i = 0
    length = len(text)
    while i < length:
        char = text[i]
        if char.isspace() or char == ",":
            i += 1
            continue
        if char != "(":
            raise boss_data.DataError(f"{where}: expected a value tuple, got {text[i:i + 20]!r}")
        i += 1
        row = []
        while True:
            while i < length and text[i].isspace():
                i += 1
            if i >= length:
                raise boss_data.DataError(f"{where}: unterminated value tuple")
            if text[i] == "'":
                i += 1
                chars = []
                while True:
                    if i >= length:
                        raise boss_data.DataError(f"{where}: unterminated string")
                    if text[i] == "'":
                        if i + 1 < length and text[i + 1] == "'":
                            chars.append("'")
                            i += 2
                            continue
                        i += 1
                        break
                    if text[i] == "\\" and i + 1 < length:
                        chars.append(text[i + 1])
                        i += 2
                        continue
                    chars.append(text[i])
                    i += 1
                row.append("".join(chars))
            else:
                match = re.match(r"-?\d+(?:\.\d+)?|NULL\b", text[i:], re.I)
                if not match:
                    raise boss_data.DataError(
                        f"{where}: unsupported value {text[i:i + 20]!r}"
                    )
                token = match.group(0)
                i += len(token)
                if token.upper() == "NULL":
                    row.append(None)
                elif "." in token:
                    row.append(float(token))
                else:
                    row.append(int(token))
            while i < length and text[i].isspace():
                i += 1
            if i < length and text[i] == ",":
                i += 1
                continue
            if i < length and text[i] == ")":
                i += 1
                break
            raise boss_data.DataError(f"{where}: expected , or ) in a value tuple")
        values.append(row)
    return values


def _convert(value, kind, where):
    if kind is str:
        if value is None:
            return ""
        if not isinstance(value, str):
            return str(value)
        return value
    if value is None:
        return 0
    number = value
    if isinstance(number, str):
        if not re.fullmatch(r"-?\d+", number):
            raise boss_data.DataError(f"{where}: {number!r} is not a number")
        number = int(number)
    if kind is int:
        if isinstance(number, float):
            if not number.is_integer():
                raise boss_data.DataError(f"{where}: {number!r} is not an integer")
            return int(number)
        return int(number)
    return float(number)


def _row_from(columns, values, fields, defaults, where):
    row = dict(defaults)
    for column, value in zip(columns, values):
        spec = fields.get(column)
        if spec is None:
            continue
        name, kind = spec
        row[name] = _convert(value, kind, where)
    for column in fields:
        name = fields[column][0]
        if name not in row and name not in defaults:
            raise boss_data.DataError(f"{where}: missing column {column}")
    return row


def _conditions(text, fields, where):
    wanted = []
    for part in re.split(r"\s+AND\s+", text.strip(), flags=re.I):
        match = re.fullmatch(r"`(\w+)`\s*=\s*(.+)", part.strip(), re.S)
        if match:
            column, raw = match.groups()
            values = [_parse_condition_value(raw, where)]
        elif ABS_RE.fullmatch(part.strip()):
            column, center, tolerance = ABS_RE.fullmatch(part.strip()).groups()
            spec = fields.get(column)
            if spec is None or spec[1] is not float:
                raise boss_data.DataError(f"{where}: unknown column {column!r}")
            wanted.append((spec[0], [[float(center)], ("abs", float(tolerance))]))
            continue
        else:
            match = re.fullmatch(r"`(\w+)`\s+IN\s*\((.+)\)", part.strip(), re.I | re.S)
            if not match:
                raise boss_data.DataError(f"{where}: unsupported WHERE clause {part.strip()!r}")
            column, raw = match.groups()
            values = [_parse_condition_value(item.strip(), where) for item in raw.split(",")]
        spec = fields.get(column)
        if spec is None:
            raise boss_data.DataError(f"{where}: unknown column {column!r}")
        name, kind = spec
        wanted.append((name, [values, kind]))
    return wanted


def _parse_condition_value(raw, where):
    raw = raw.strip()
    if raw.startswith("'") and raw.endswith("'"):
        return raw[1:-1].replace("''", "'")
    if re.fullmatch(r"-?\d+", raw):
        return int(raw)
    if re.fullmatch(r"-?\d+\.\d+", raw):
        return float(raw)
    raise boss_data.DataError(f"{where}: unsupported value {raw!r} in a WHERE clause")


def _mask_strings(text):
    """Blank out quoted text, so a table name inside a literal is not a use."""
    out = []
    i = 0
    length = len(text)
    quote = False
    while i < length:
        char = text[i]
        if quote:
            out.append(" ")
            if char == "'":
                if i + 1 < length and text[i + 1] == "'":
                    out.append(" ")
                    i += 2
                    continue
                quote = False
            elif char == "\\" and i + 1 < length:
                out.append(" ")
                i += 2
                continue
            i += 1
        elif char == "'":
            quote = True
            out.append(" ")
            i += 1
        else:
            out.append(char)
            i += 1
    return "".join(out)


def _split_assignments(text):
    """The `col = value, col = value` list, split on the commas outside quotes."""
    parts = []
    buf = []
    i = 0
    length = len(text)
    quote = False
    while i < length:
        char = text[i]
        if quote:
            buf.append(char)
            if char == "'":
                if i + 1 < length and text[i + 1] == "'":
                    buf.append("'")
                    i += 2
                    continue
                quote = False
            elif char == "\\" and i + 1 < length:
                buf.append(text[i + 1])
                i += 2
                continue
            i += 1
        elif char == "'":
            quote = True
            buf.append(char)
            i += 1
        elif char == ",":
            parts.append("".join(buf))
            buf = []
            i += 1
        else:
            buf.append(char)
            i += 1
    parts.append("".join(buf))
    return [part.strip() for part in parts if part.strip()]


def _set_values(text, fields, where):
    """The UPDATE's tracked SET values as {row field: value}; other columns are ignored."""
    changes = {}
    for part in _split_assignments(text):
        match = re.fullmatch(r"`(\w+)`\s*=\s*(.+)", part, re.S)
        if not match:
            raise boss_data.DataError(f"{where}: unsupported SET clause {part!r}")
        column, raw = match.groups()
        spec = fields.get(column)
        if spec is None:
            continue
        name, kind = spec
        changes[name] = _convert(_parse_condition_value(raw, where), kind, where)
    return changes


def _touches_tracked(statement):
    """True when the statement names one of the replayed tables outside a string literal."""
    masked = _mask_strings(statement)
    return any(re.search(r"`%s`" % re.escape(table), masked) for table in SQL_TABLES)


def _matches(row, wanted):
    for name, (values, kind) in wanted:
        value = row.get(name)
        if isinstance(kind, tuple):
            if not abs(float(value) - values[0]) < kind[1]:
                return False
            continue
        if not any(_convert(candidate, kind, "WHERE") == value for candidate in values):
            return False
    return True


def _missing_table(exc):
    return bool(getattr(exc, "args", None)) and exc.args[0] == 1146


def parse_kill_first(text):
    """The plan list as [entry, ...]; None when the list is empty or unparsable."""
    entries = []
    for part in (text or "").split(","):
        part = part.strip()
        if not part:
            continue
        if not re.fullmatch(r"\d+", part):
            return None
        value = int(part)
        if value > 0:
            entries.append(value)
    return entries or None


def tactic_rows(path):
    """Effective ai_boss_tactic rows of a migration directory or a repo root, sorted."""
    directory = Path(path)
    default = directory / "data" / "sql" / "world"
    return migration_rows(default if default.is_dir() else directory)["tactics"]


def _stmt_where(path, statement):
    return f"{Path(path).name}: {statement[:60]!r}"


def migration_rows(sql_dir):
    """Replay the migrations in file order: INSERT/REPLACE, the scoped DELETEs and the row UPDATEs.

    A row statement on one of the three tables that the replay cannot apply is
    an error naming the file and the statement, never a silent skip.
    """
    directory = Path(sql_dir)
    paths = sorted(directory.glob("*.sql"), key=lambda p: p.name)
    tactics = {}
    stands = []
    plans = {}
    steps = {}
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        if not any(table in text for table in SQL_TABLES):
            continue
        for statement in _split_statements(_strip_comments(text)):
            where = _stmt_where(path, statement)
            match = INSERT_RE.match(statement)
            if match:
                table, columns_text, values_text = match.groups()
                if table not in SQL_TABLES:
                    continue
                columns = [c.strip().strip("`") for c in columns_text.split(",")]
                values = _parse_values(values_text, where)
                for tuple_values in values:
                    if len(tuple_values) != len(columns):
                        raise boss_data.DataError(
                            f"{where}: {len(tuple_values)} values for {len(columns)} columns"
                        )
                    if table == "ai_boss_tactic":
                        row = _row_from(columns, tuple_values, TACTIC_FIELDS, {}, where)
                        key = (row["map"], row["boss"], row["kind"], row["target"], row["spell"])
                        tactics[key] = row
                    elif table == "ai_boss_stand":
                        row = _row_from(
                            columns, tuple_values, STAND_FIELDS, STAND_DEFAULTS, where
                        )
                        stands.append(row)
                    elif table == "ai_dungeon_step":
                        row = _row_from(columns, tuple_values, STEP_FIELDS, STEP_DEFAULTS, where)
                        steps[(row["plan"], row["leg"], row["seq"])] = row
                    else:
                        row = _row_from(columns, tuple_values, PLAN_FIELDS, PLAN_DEFAULTS, where)
                        plans[row["plan"]] = row
                continue
            match = DELETE_RE.match(statement)
            if match:
                table, conditions_text = match.groups()
                if table not in SQL_TABLES:
                    continue
                if table == "ai_boss_tactic":
                    wanted = _conditions(conditions_text, TACTIC_FIELDS, where)
                    for key in [k for k, row in tactics.items() if _matches(row, wanted)]:
                        del tactics[key]
                elif table == "ai_boss_stand":
                    wanted = _conditions(conditions_text, STAND_FIELDS, where)
                    stands = [row for row in stands if not _matches(row, wanted)]
                elif table == "ai_dungeon_step":
                    wanted = _conditions(conditions_text, STEP_FIELDS, where)
                    for key in [k for k, row in steps.items() if _matches(row, wanted)]:
                        del steps[key]
                else:
                    wanted = _conditions(conditions_text, PLAN_FIELDS, where)
                    for key in [k for k, row in plans.items() if _matches(row, wanted)]:
                        del plans[key]
                continue
            match = UPDATE_RE.match(statement)
            if match:
                table, set_text, conditions_text = match.groups()
                if table not in SQL_TABLES:
                    continue
                if table == "ai_boss_tactic":
                    changes = _set_values(set_text, TACTIC_FIELDS, where)
                    wanted = _conditions(conditions_text, TACTIC_FIELDS, where)
                    updated = {}
                    for row in tactics.values():
                        if _matches(row, wanted):
                            row = dict(row)
                            row.update(changes)
                        key = (row["map"], row["boss"], row["kind"], row["target"], row["spell"])
                        updated[key] = row
                    tactics = updated
                elif table == "ai_boss_stand":
                    changes = _set_values(set_text, STAND_FIELDS, where)
                    wanted = _conditions(conditions_text, STAND_FIELDS, where)
                    updated = []
                    for row in stands:
                        if _matches(row, wanted):
                            row = dict(row)
                            row.update(changes)
                        updated.append(row)
                    stands = updated
                elif table == "ai_dungeon_step":
                    changes = _set_values(set_text, STEP_FIELDS, where)
                    wanted = _conditions(conditions_text, STEP_FIELDS, where)
                    updated = {}
                    for row in steps.values():
                        if _matches(row, wanted):
                            row = dict(row)
                            row.update(changes)
                        updated[(row["plan"], row["leg"], row["seq"])] = row
                    steps = updated
                else:
                    changes = _set_values(set_text, PLAN_FIELDS, where)
                    wanted = _conditions(conditions_text, PLAN_FIELDS, where)
                    updated = {}
                    for row in plans.values():
                        if _matches(row, wanted):
                            row = dict(row)
                            row.update(changes)
                        updated[row["plan"]] = row
                    plans = updated
                continue
            keyword = re.match(r"\s*(\w+)", statement)
            if _touches_tracked(statement) and (
                keyword is None or keyword.group(1).upper() not in SCHEMA_KEYWORDS
            ):
                raise boss_data.DataError(f"{where}: unsupported statement")
            if re.match(r"^\s*(?:INSERT|REPLACE|DELETE|UPDATE|TRUNCATE)\b", statement, re.I):
                raise boss_data.DataError(f"{where}: unsupported statement")
    plan_maps = {name: row["map"] for name, row in plans.items()}
    boss_steps = [
        {"plan": row["plan"], "map": plan_maps[row["plan"]], "boss": row["entry"], "killFirst": row["entries"]}
        for row in steps.values()
        if row["kind"] == "boss" and row["plan"] in plan_maps
    ]
    return _bundle(
        tactics.values(), stands, plans.values(), "migrations", len(paths), boss_steps
    )


def _aggregate_stands(rows):
    grouped = {}
    for row in rows:
        key = (
            row["map"],
            row["boss"],
            row["windowSpell"],
            row["aura"],
            row["role"],
            row["signal"],
        )
        if key not in grouped:
            grouped[key] = {
                "map": row["map"],
                "boss": row["boss"],
                "windowSpell": row["windowSpell"],
                "aura": row["aura"],
                "role": row["role"],
                "signal": row["signal"],
                "seats": 0,
            }
        grouped[key]["seats"] += row["slots"]
    return list(grouped.values())


def _bundle(tactics, stands, plans, source, files, boss_steps=()):
    tactic_list = sorted(
        tactics, key=lambda r: (r["map"], r["boss"], r["kind"], r["target"], r["spell"])
    )
    stand_list = sorted(
        _aggregate_stands(stands),
        key=lambda r: (r["map"], r["boss"], r["windowSpell"], r["role"], r["aura"], r["signal"]),
    )
    plan_list = []
    for plan in sorted(plans, key=lambda p: p["plan"]):
        entries = parse_kill_first(plan["killFirst"])
        if entries is None:
            continue
        plan_list.append({"plan": plan["plan"], "map": plan["map"], "entries": entries})
    step_list = []
    for step in sorted(boss_steps, key=lambda s: (s["plan"], s["boss"])):
        entries = parse_kill_first(step["killFirst"])
        if entries is None:
            continue
        step_list.append(
            {"plan": step["plan"], "map": step["map"], "boss": step["boss"], "entries": entries}
        )
    return {
        "tactics": tactic_list,
        "stands": stand_list,
        "planKillFirst": plan_list,
        "bossKillFirst": step_list,
        "source": source,
        "loadedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "files": files,
    }


def _db_query(connect, query):
    connection = connect()
    try:
        cursor = connection.cursor()
        try:
            cursor.execute(query)
            return cursor.fetchall()
        except Exception as exc:
            if _missing_table(exc):
                return None
            raise
    finally:
        connection.close()


def db_rows(connect):
    """The same three reads as the daemon, through a boss_data.db_connect factory."""
    tactics = []
    for row in _db_query(connect, QUERY_TACTICS) or ():
        tactics.append(
            {
                "map": int(row[0]),
                "boss": int(row[1]),
                "kind": row[2],
                "target": int(row[3]),
                "spell": int(row[4]),
                "priority": int(row[5]),
                "param": float(row[6]),
                "comment": row[7] or "",
            }
        )
    stands = []
    for row in _db_query(connect, QUERY_STANDS) or ():
        stands.append(
            {
                "map": int(row[0]),
                "boss": int(row[1]),
                "windowSpell": int(row[2]),
                "aura": int(row[3]),
                "role": row[4],
                "signal": row[5],
                "slots": int(row[6]),
            }
        )
    plans = []
    for row in _db_query(connect, QUERY_PLAN_KILL_FIRST) or ():
        plans.append({"plan": row[0], "map": int(row[1]), "killFirst": row[2] or ""})
    boss_steps = []
    for row in _db_query(connect, QUERY_STEPS) or ():
        if row[2] == "boss":
            boss_steps.append(
                {"plan": row[0], "map": int(row[1]), "boss": int(row[3]), "killFirst": row[4] or ""}
            )
    return _bundle(tactics, stands, plans, "world", None, boss_steps)


# --------------------------------------------------------------------------
# The coverage rule


def _derived_needs(ability):
    needs = []
    if ability.get("int"):
        needs.append("interrupt")
    if ability.get("summon"):
        needs.append("kill")
    if ability.get("cone"):
        needs.append("front")
    if ability.get("tail"):
        needs.append("tail")
    if ability.get("ground"):
        needs.append("avoid")
    if ability.get("mark"):
        needs.append("mark")
    if ability.get("aura"):
        needs.append("dispel")
    if ability.get("self"):
        needs.append("purge")
    return needs


def _code_text(answer):
    prefix = "generic" if answer.get("map") == "*" else "code"
    return f"{prefix}: {answer['name']}"


def _stand_text(stands):
    by_role = {}
    for stand in stands:
        by_role[stand["role"]] = by_role.get(stand["role"], 0) + stand["seats"]
    parts = [f"{role} {seats}" for role, seats in sorted(by_role.items())]
    total = sum(by_role.values())
    unit = "seat" if total == 1 else "seats"
    return "stand set: " + ", ".join(parts) + " " + unit


def _status(levels, ok_all):
    if ok_all:
        return "notneeded"
    if not levels:
        return "missing"
    real = [level for level in levels.values() if level != "ok"]
    if not real:
        return "notneeded"
    if all(level == "full" for level in real):
        return "answered"
    if any(level in ("full", "partly") for level in real):
        return "partly"
    if any(level == "unknown" for level in real):
        return "unknown"
    return "missing"


def _classify_text():
    return "the table does not say what it needs (add a need flag or ok=<reason>)"


class _Boss:
    def __init__(self, doc, map_id, boss_lookup, tactics, stands, plans, codes, have_rows, steps=()):
        self.doc = doc
        self.entry = doc["entry"]
        self.map_id = map_id
        self.boss_lookup = boss_lookup
        self.codes = codes
        self.have_rows = have_rows
        self.sids = {ability["sid"] for ability in doc.get("abilities") or []}
        self.map_tactics = [t for t in tactics if t["map"] == map_id]
        self.tactics = [t for t in self.map_tactics if t["boss"] == self.entry]
        self.stands = [s for s in stands if s["map"] == map_id and s["boss"] == self.entry]
        self.kills = [t for t in tactics if t["map"] == map_id and t["kind"] == "kill_first"]
        self.plans = [p for p in plans if p["map"] == map_id]
        self.steps = [s for s in steps if s["map"] == map_id]
        self.warnings = []
        self.used_tactics = set()
        self.used_stands = set()

    def warn(self, text):
        if text not in self.warnings:
            self.warnings.append(text)

    def _other_boss(self, entry):
        other = self.boss_lookup.get(str(entry))
        if other:
            return f"{other['name']} ({entry})"
        return str(entry)

    def _code_matches(self, answer, ability=None, sid=0, add_entry=0):
        if answer.get("map") not in ("*", self.doc["map"]):
            return False
        if answer.get("boss") not in (0, self.entry):
            return False
        if add_entry:
            return answer.get("add") == add_entry
        if answer.get("spell"):
            return answer.get("spell") == sid
        if answer.get("flag"):
            return bool(ability and ability.get(answer["flag"]))
        return False

    def ability_line(self, ability):
        sid = ability["sid"]
        derived = _derived_needs(ability)
        needs = list(derived)
        interrupt_rows = []
        for row in self.map_tactics:
            if row["kind"] != "interrupt" or row["spell"] != sid:
                continue
            belongs = row["boss"] == self.entry
            matches = (belongs and row["target"] in (0, self.entry)) or row["target"] == self.entry
            if belongs:
                self.used_tactics.add(id(row))
            if matches or belongs:
                interrupt_rows.append((row, matches))
        if interrupt_rows and "interrupt" not in needs:
            needs.insert(0, "interrupt")
            if not ability.get("int"):
                self.warn(
                    f"interrupt row on {sid} {ability['name']}: the table does not flag it int; "
                    "check that the core can interrupt it"
                )
        stands_here = [s for s in self.stands if s["windowSpell"] == sid]
        if stands_here:
            self.used_stands.update(id(s) for s in stands_here)
            if not any(need in derived for need in ("front", "tail", "avoid", "mark")):
                if "avoid" not in needs:
                    needs.append("avoid")

        levels = {need: "none" for need in needs}
        answers = {need: [] for need in needs}
        ok_all = ability.get("ok") or ""
        ok_needs = ability.get("okNeeds") or {}

        def answer(need, level, text):
            if need not in levels:
                levels[need] = "none"
                answers[need] = []
                needs.append(need)
            if LEVEL_ORDER[level] > LEVEL_ORDER.get(levels[need], 0):
                levels[need] = level
            answers[need].append((level, text))

        if self.have_rows:
            for row, matches in interrupt_rows:
                if matches:
                    text = f"interrupt row ({row['comment']})" if row["comment"] else "interrupt row"
                    answer("interrupt", "full", text)
            if stands_here:
                text = _stand_text(stands_here)
                for need in ("front", "tail", "avoid", "mark"):
                    if need in levels:
                        answer(need, "full", text)

        if ability.get("summon"):
            self._kill_answers(ability["summon"], answer)
        for row in self.tactics:
            if row["kind"] == "behind" and self.have_rows and ability.get("cone"):
                answer("front", "full", f"behind {row['param']:g} yd")

        for code in self.codes:
            if not self._code_matches(code, ability=ability, sid=sid):
                continue
            need = code["need"]
            answer(need, code["level"], _code_text(code))

        if ok_all:
            for need in levels:
                levels[need] = "ok"
        else:
            for need, _reason in ok_needs.items():
                if need in levels:
                    levels[need] = "ok"
        if not levels and not ok_all:
            levels["classify"] = "none"
            answers["classify"] = []
            needs = ["classify"]
        if not self.have_rows:
            for need in levels:
                if need in DB_NEEDS and levels[need] not in ("ok", "full"):
                    levels[need] = "unknown"

        ordered = {need: levels[need] for need in NEED_ORDER if need in levels}
        answer_list = []
        for need in ordered:
            for level, text in answers.get(need, ()):
                answer_list.append({"need": need, "level": level, "text": text})
        return {
            "key": str(sid),
            "kind": "ability",
            "sid": sid,
            "add": 0,
            "name": ability["name"],
            "phase": ability.get("phase") or "",
            "status": _status(ordered, ok_all),
            "needs": ordered,
            "answers": answer_list,
            "ok": ok_all,
            "okNeeds": dict(ok_needs),
        }

    def _kill_answers(self, entry, answer):
        if self.have_rows:
            for row in self.kills:
                if row["target"] != entry:
                    continue
                if row["boss"] == self.entry:
                    answer("kill", "full", f"kill_first {entry} (prio {row['priority']})")
                    self.used_tactics.add(id(row))
                else:
                    answer(
                        "kill",
                        "partly",
                        f"kill_first {entry} (row under boss {self._other_boss(row['boss'])})",
                    )
                    self.used_tactics.add(id(row))
                    self.warn(
                        f"kill_first on {entry} is under boss {self._other_boss(row['boss'])}: "
                        "check it is the right boss"
                    )
            for step in self.steps:
                if entry not in step["entries"]:
                    continue
                if step["boss"] == self.entry:
                    answer("kill", "full", f"plan {step['plan']} boss step kill list")
                else:
                    answer(
                        "kill",
                        "partly",
                        f"plan {step['plan']} boss step of {self._other_boss(step['boss'])}",
                    )
            for plan in self.plans:
                if entry in plan["entries"]:
                    answer("kill", "partly", f"plan {plan['plan']} kill_first (run leader only)")
        for code in self.codes:
            if self._code_matches(code, add_entry=entry):
                answer("kill", code["level"], _code_text(code))

    def add_line(self, add):
        entry = add["entry"]
        levels = {"kill": "none"}
        answers = {"kill": []}

        def answer(need, level, text):
            if LEVEL_ORDER[level] > LEVEL_ORDER.get(levels[need], 0):
                levels[need] = level
            answers[need].append((level, text))

        self._kill_answers(entry, answer)
        if not self.have_rows and levels["kill"] != "full":
            levels["kill"] = "unknown"
        return {
            "key": f"add:{entry}",
            "kind": "add",
            "sid": 0,
            "add": entry,
            "name": add["name"],
            "phase": "",
            "status": _status(levels, ""),
            "needs": dict(levels),
            "answers": [
                {"need": need, "level": level, "text": text}
                for need, items in answers.items()
                for level, text in items
            ],
            "ok": "",
            "okNeeds": {},
        }

    def extras(self):
        has_cone = any(ability.get("cone") for ability in self.doc.get("abilities") or [])
        extra = []
        for row in self.tactics:
            if id(row) in self.used_tactics:
                continue
            if row["kind"] == "interrupt" and self.have_rows:
                used = row["spell"] in self.sids
                if used:
                    continue
                why = f"no ability with spell {row['spell']} in the table"
            elif row["kind"] == "behind" and self.have_rows:
                if has_cone:
                    continue
                if row["spell"] in self.sids:
                    why = "boss has no cone ability"
                else:
                    why = f"no ability with spell {row['spell']} in the table"
            elif row["kind"] == "kill_first" and self.have_rows:
                why = (
                    f"kill_first on entry {row['target']}: neither a summon nor an add of the table"
                )
            elif row["kind"] == "air_target" and self.have_rows:
                why = "air_target: keeps the boss a target in its air phase (not an ability answer)"
            else:
                continue
            extra.append(
                {
                    "key": _tactic_key(row),
                    "ident": _tactic_ident(row),
                    "why": why,
                }
            )
        for stand in self.stands:
            if id(stand) in self.used_stands:
                continue
            if stand["windowSpell"] in self.sids:
                continue
            if stand["windowSpell"] == 0:
                why = "window_spell 0: applies always, not an ability answer"
            else:
                why = f"no ability with spell {stand['windowSpell']} in the table"
            extra.append(
                {
                    "key": f"stand:{stand['windowSpell']}",
                    "ident": f"stand {stand['windowSpell']}",
                    "why": why,
                }
            )
        return _dedupe_extras(extra)

    def result(self):
        lines = []
        for ability in self.doc.get("abilities") or []:
            lines.append(self.ability_line(ability))
        for add in self.doc.get("adds") or []:
            if add.get("priority", 0) > 0:
                lines.append(self.add_line(add))
        counts = dict.fromkeys(COUNT_KEYS, 0)
        for line in lines:
            counts[line["status"]] += 1
        return {
            "entry": self.entry,
            "name": self.doc["name"],
            "map": self.doc["map"],
            "lines": lines,
            "warnings": list(self.warnings),
            "extra": self.extras(),
            "counts": counts,
        }


def _dedupe_extras(extra):
    seen = set()
    out = []
    for item in extra:
        if item["key"] in seen:
            continue
        seen.add(item["key"])
        out.append(item)
    return out


def _tactic_ident(row):
    if row["kind"] == "kill_first":
        return f"kill_first {row['target']}"
    return f"{row['kind']} {row['spell']}"


def _tactic_key(row):
    ident = row["target"] if row["kind"] == "kill_first" else row["spell"]
    return f"tactic:{row['kind']}:{ident}"


def compute(table, rows):
    """The coverage result for the built table and the replayed rows (None = unread)."""
    maps = table.get("maps") or []
    bosses = table.get("bosses") or {}
    codes = table.get("codeAnswers") or []
    have_rows = rows is not None
    tactics = list((rows or {}).get("tactics") or [])
    stands = list((rows or {}).get("stands") or [])
    plans = list((rows or {}).get("planKillFirst") or [])
    steps = list((rows or {}).get("bossKillFirst") or [])
    raids = []
    for raid in maps:
        map_id = raid.get("mapId")
        entries = [e for e in (raid.get("bosses") or []) if str(e) in bosses]
        boss_results = []
        for entry in entries:
            doc = bosses[str(entry)]
            worker = _Boss(doc, map_id, bosses, tactics, stands, plans, codes, have_rows, steps)
            boss_results.append(worker.result())
        unknown = {}
        if have_rows:
            for row in tactics:
                if row["map"] == map_id and str(row["boss"]) not in bosses:
                    unknown.setdefault(row["boss"], []).append(
                        {
                            "key": _tactic_key(row),
                            "ident": _tactic_ident(row),
                            "why": f"boss {row['boss']} is not in the table",
                        }
                    )
            for stand in stands:
                if stand["map"] == map_id and str(stand["boss"]) not in bosses:
                    unknown.setdefault(stand["boss"], []).append(
                        {
                            "key": f"stand:{stand['windowSpell']}",
                            "ident": f"stand {stand['windowSpell']}",
                            "why": f"boss {stand['boss']} is not in the table",
                        }
                    )
        counts = dict.fromkeys(COUNT_KEYS, 0)
        for boss_result in boss_results:
            for key in COUNT_KEYS:
                counts[key] += boss_result["counts"][key]
        raids.append(
            {
                "key": raid.get("key"),
                "mapId": map_id,
                "name": raid.get("name"),
                "counts": counts,
                "bosses": boss_results,
                "unknownBosses": [
                    {"entry": entry, "extra": _dedupe_extras(unknown[entry])}
                    for entry in sorted(unknown)
                ],
            }
        )
    return {
        "raids": raids,
        "source": (rows or {}).get("source") if have_rows else None,
        "loadedAt": (rows or {}).get("loadedAt") if have_rows else None,
        "files": (rows or {}).get("files") if have_rows else None,
    }


def project(result):
    """The fixture projection of a compute result (data/boss/testdata/coverage_cases.json)."""
    out = {}
    zeros = dict.fromkeys(COUNT_KEYS, 0)
    for raid in result.get("raids") or []:
        for boss in raid.get("bosses") or []:
            out[str(boss["entry"])] = {
                "lines": {
                    line["key"]: {"status": line["status"], "needs": dict(line["needs"])}
                    for line in boss["lines"]
                },
                "extra": [extra["key"] for extra in boss["extra"]],
                "warnings": len(boss["warnings"]),
                "counts": dict(boss["counts"]),
            }
        for unknown in raid.get("unknownBosses") or []:
            out[str(unknown["entry"])] = {
                "lines": {},
                "extra": [extra["key"] for extra in unknown["extra"]],
                "warnings": 0,
                "counts": dict(zeros),
            }
    return out


# --------------------------------------------------------------------------
# Text report


def _counts_text(counts):
    parts = [
        f"{counts['answered']} answered",
        f"{counts['partly']} partly",
        f"{counts['missing']} missing",
        f"{counts['notneeded']} not needed",
    ]
    if counts.get("unknown"):
        parts.append(f"{counts['unknown']} unknown")
    return ", ".join(parts)


def _source_label(result):
    if result.get("source") == "world":
        return f"rows: world, read {result.get('loadedAt')}"
    if result.get("source") == "migrations":
        files = result.get("files") or 0
        unit = "file" if files == 1 else "files"
        return f"rows: migrations, {files} {unit}"
    return "rows: none"


def _line_summary(line):
    parts = []
    for need, level in line["needs"].items():
        if need == "classify":
            parts.append(f"classify: {_classify_text()}")
            continue
        if level == "ok":
            reason = (line.get("okNeeds") or {}).get(need) or line.get("ok") or ""
            parts.append(f"{need}: ok ({reason})" if reason else f"{need}: ok")
            continue
        if level == "none":
            parts.append(f"{need}: no row")
            continue
        if level == "unknown":
            parts.append(f"{need}: no row read")
            continue
        texts = [a["text"] for a in line["answers"] if a["need"] == need]
        parts.append(f"{need}: " + (", ".join(texts) if texts else level))
    return "; ".join(parts)


def _line_ident(line):
    return f"add {line['add']}" if line["kind"] == "add" else str(line["sid"])


def _line_name(line):
    return line["name"] + (f" ({line['phase']})" if line.get("phase") else "")


STATUS_LABEL = {"answered": "answered", "partly": "partly", "missing": "missing",
                "notneeded": "ok", "unknown": "unknown"}


def report_text(result, all_lines=False, boss=0):
    out = []
    for raid in result.get("raids") or []:
        bosses_out = [
            item for item in raid.get("bosses") or [] if not boss or item["entry"] == boss
        ]
        unknown = [
            item for item in raid.get("unknownBosses") or [] if not boss or item["entry"] == boss
        ]
        if boss and not bosses_out and not unknown:
            continue
        counts = raid["counts"]
        if boss:
            counts = dict.fromkeys(COUNT_KEYS, 0)
            for item in bosses_out:
                for key in COUNT_KEYS:
                    counts[key] += item["counts"][key]
        total = sum(counts[key] for key in COUNT_KEYS)
        out.append(
            f"{raid['name'] or raid['key']} (map {raid['mapId']}): {total} lines: "
            f"{_counts_text(counts)}   [{_source_label(result)}]"
        )
        for item in bosses_out:
            shown = [
                line
                for line in item["lines"]
                if all_lines or line["status"] not in ("answered", "notneeded")
            ]
            if not (all_lines or boss or shown or item["extra"] or item["warnings"]):
                continue
            out.append(f"{item['name']} ({item['entry']}): {_counts_text(item['counts'])}")
            for line in shown:
                out.append(
                    f"  {STATUS_LABEL[line['status']]:<8} {_line_ident(line):<6} "
                    f"{_line_name(line):<22} {_line_summary(line)}"
                )
            for extra in item["extra"]:
                out.append(f"  {'row':<8} {extra['ident']:<30} {extra['why']}")
            for warning in item["warnings"]:
                out.append(f"  {'warning':<8} {warning}")
        if unknown:
            out.append("  rows for bosses the table does not list:")
            for item in unknown:
                for extra in item["extra"]:
                    out.append(
                        f"    boss {item['entry']}  {extra['key']}  {extra['why']}"
                    )
    return "\n".join(out) + ("\n" if out else "")


# --------------------------------------------------------------------------
# CLI


def _load_table(args):
    return boss_data.build_document(
        boss_data.read_rows(Path(args.csv)),
        boss_data.read_names(Path(args.names)),
        boss_data.read_raids(Path(args.raids)),
        boss_data.read_code_answers(Path(args.code_answers)),
        args.repo_root,
    )


def _resolve_raid(table, wanted):
    for raid in table.get("maps") or []:
        if wanted == raid.get("key") or wanted == str(raid.get("mapId")):
            return raid.get("key")
    return None


def _missing_lines(result):
    for raid in result.get("raids") or []:
        for boss in raid.get("bosses") or []:
            for line in boss["lines"]:
                if line["status"] == "missing":
                    return True
    return False


def cmd_coverage(args):
    """`boss_data.py coverage`: build the table from the CSV, replay or read the rows, report."""
    try:
        table = _load_table(args)
        if getattr(args, "db", False):
            rows = db_rows(lambda: boss_data.db_connect(args))
        else:
            rows = migration_rows(args.sql_dir)
    except boss_data.DataError as exc:
        print(f"coverage: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # DB driver and connection errors are report failures
        print(f"coverage: {exc}", file=sys.stderr)
        return 2

    wanted = args.raid or ""
    if wanted:
        key = _resolve_raid(table, wanted)
        if key is None:
            known = ", ".join(raid["key"] for raid in table.get("maps") or [])
            print(f"no raid {wanted}; known: {known}", file=sys.stderr)
            return 2
    result = compute(table, rows)
    if wanted:
        result["raids"] = [raid for raid in result["raids"] if raid["key"] == key]
    if args.boss:
        for raid in result["raids"]:
            raid["bosses"] = [b for b in raid["bosses"] if b["entry"] == args.boss]
            raid["unknownBosses"] = [
                u for u in raid["unknownBosses"] if u["entry"] == args.boss
            ]
            counts = dict.fromkeys(COUNT_KEYS, 0)
            for boss in raid["bosses"]:
                for count_key in COUNT_KEYS:
                    counts[count_key] += boss["counts"][count_key]
            raid["counts"] = counts

    if args.json:
        print(json.dumps(result, sort_keys=True, indent=2))
    else:
        sys.stdout.write(report_text(result, all_lines=args.all, boss=args.boss))
    if args.fail_on_missing and _missing_lines(result):
        return 1
    return 0
