"""Guards every text value a world migration writes into a VARCHAR(N) column of a module table.

Why: on 2026-11-13 the walk 2i image rolled back on tortoise-test2 because 20261113200000_world.sql's
`UPDATE ai_boss_tactic` wrote a 317-character `comment` into a varchar(255) column. The updater runs each migration
inside one transaction and the connection is strict, so MySQL refused the statement with
"[1406] Data too long for column 'comment'" and the updater stopped: no ledger row, the world never came up, and a
build and a rollback were spent on finding it. A migration file may not be edited once applied, so the only fix for
such a value is a newer migration - it has to be caught before the build.

What it does: reads the column widths from the same tree (every CREATE TABLE and every `ALTER TABLE ... ADD/MODIFY`
of an `ai_` table, replayed in file order), then measures the single-quoted literal every INSERT/REPLACE and UPDATE
writes into a varchar/char column of one. MySQL counts characters, not bytes, so `''` and `\\'` count as one
character each. A value that is not a literal (a variable, CONCAT) is skipped: its length is not known here.

The SQL splitter and the tuple/SET parsers are the ones tools/test_boss_tactics_sql.py already uses.

Run: python3 -m unittest tools/test_migration_text_widths.py
"""
import glob
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from test_boss_tactics_sql import _selected_values, _set_parts, _sql_statements, _tuples  # noqa: E402
# The module tables; their schema is ours, so every value they take is ours too.
MODULE_PREFIX = "ai_"
TEXT_TYPES = {"varchar", "char"}

CREATE_TABLE = re.compile(r"CREATE TABLE(?: IF NOT EXISTS)?\s+`([A-Za-z_0-9]+)`\s*\((.*?)\)\s*ENGINE", re.S)
COLUMN = re.compile(r"^\s*`([A-Za-z_0-9]+)`\s+([a-z]+)(?:\s*\(\s*(\d+)(?:\s*,\s*\d+)?\s*\))?", re.M)
ALTER_COLUMN = re.compile(
    r"ALTER TABLE\s+`([A-Za-z_0-9]+)`\s+(ADD|MODIFY)\s+(?:COLUMN\s+)?`?([A-Za-z_0-9]+)`?\s+"
    r"([a-z]+)(?:\s*\(\s*(\d+)(?:\s*,\s*\d+)?\s*\))?",
    re.I,
)
INSERT = re.compile(
    r"(?:INSERT|REPLACE)\s+(?:IGNORE\s+)?INTO\s+`([A-Za-z_0-9]+)`\s*\(([^)]*)\)\s*VALUES\s*(.+)$",
    re.I | re.S,
)
UPDATE = re.compile(r"UPDATE\s+`([A-Za-z_0-9]+)`\s+SET\s+(.+?)(?:\s+WHERE\s.*)?$", re.I | re.S)
SET_PART = re.compile(r"`([A-Za-z_0-9]+)`\s*=\s*(.+)", re.S)


def migration_paths():
    return sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql")))


def column_widths(paths=None):
    """{table: {column: max characters or None}} for every `ai_` table, ALTERs applied in file order."""
    widths = {}
    for path in (migration_paths() if paths is None else paths):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_" not in text:
            continue
        for match in CREATE_TABLE.finditer(text):
            table, body = match.group(1), match.group(2)
            if not table.startswith(MODULE_PREFIX):
                continue
            columns = widths.setdefault(table, {})
            for found in COLUMN.finditer(body):
                columns[found.group(1)] = int(found.group(3)) if found.group(3) else None
        for match in ALTER_COLUMN.finditer(text):
            table, column, size = match.group(1), match.group(3), match.group(5)
            if table in widths:
                widths[table][column] = int(size) if size else None
    return widths


def literal_length(raw):
    """The characters in a single-quoted SQL literal, or None when the value is not a plain literal."""
    raw = raw.strip()
    if not raw.startswith("'"):
        return None
    decoded = []
    index = 1
    size = len(raw)
    while index < size:
        char = raw[index]
        if char == "\\" and index + 1 < size:
            decoded.append(raw[index + 1])
            index += 2
            continue
        if char == "'":
            if index + 1 < size and raw[index + 1] == "'":
                decoded.append("'")
                index += 2
                continue
            return len(decoded)  # the closing quote; anything after it is an operator or a comment
        decoded.append(char)
        index += 1
    return None  # no closing quote: the statement is already broken in another way


def overlong_values(paths=None, widths=None):
    """(file, table, column, limit, length, value) for each literal that does not fit its column."""
    widths = column_widths(paths) if widths is None else widths
    overlong = []
    measured = [0]

    def check(name, table, column, raw):
        limit = widths.get(table, {}).get(column)
        if limit is None:
            return
        length = literal_length(raw)
        if length is None:
            return
        measured[0] += 1
        if length > limit:
            overlong.append((name, table, column, limit, length, raw.strip()[1:81]))

    for path in (migration_paths() if paths is None else paths):
        name = os.path.basename(path)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_" not in text:
            continue
        for statement in _sql_statements(text):
            found = INSERT.match(statement)
            if found and found.group(1).startswith(MODULE_PREFIX):
                table = found.group(1)
                columns = [column.strip().strip("`") for column in found.group(2).split(",")]
                for tuple_text in _tuples(found.group(3)):
                    values = _selected_values(tuple_text, set(range(len(columns))))
                    for index, column in enumerate(columns):
                        if index in values:
                            check(name, table, column, values[index])
                continue
            found = UPDATE.match(statement)
            if found and found.group(1).startswith(MODULE_PREFIX):
                table = found.group(1)
                for part in _set_parts(found.group(2).strip()):
                    assignment = SET_PART.fullmatch(part)
                    if assignment:
                        check(name, table, assignment.group(1), assignment.group(2))
    return overlong, measured[0]


class MigrationTextWidths(unittest.TestCase):
    def test_every_literal_fits_its_column(self):
        # 20261113200000_world.sql held a 317-character tank_post comment in ai_boss_tactic.comment (varchar(255)).
        overlong, _measured = overlong_values()
        self.assertEqual(
            [], ["%s: %s.%s is varchar(%d), the value is %d chars: %s..." % row for row in overlong]
        )

    def test_the_widths_are_read_from_the_migrations(self):
        widths = column_widths()
        self.assertEqual(255, widths["ai_boss_tactic"]["comment"])
        self.assertEqual(16, widths["ai_boss_tactic"]["kind"])
        self.assertEqual(255, widths["ai_boss_stand"]["comment"])
        self.assertEqual(255, widths["ai_dungeon_step"]["entries"])
        self.assertEqual(255, widths["ai_dungeon_leg_need"]["how"])
        # Columns the migrations add rather than create: a varchar added by ALTER carries its width there.
        self.assertEqual(96, widths["ai_boss_tactic"]["burn_order"])
        self.assertEqual(255, widths["ai_dungeon_plan"]["cc_priority"])

    def test_the_scan_reaches_the_row_data(self):
        # A guard that reads no values passes everything: the 601 Deep Breath seats alone are 601 stand rows.
        _overlong, measured = overlong_values()
        self.assertGreater(measured, 1000, "the scan measured almost no text values: the parsers lost the rows")


if __name__ == "__main__":
    unittest.main()
