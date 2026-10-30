"""Tests for tools/boss_coverage.py: the rule, the replay and the CLI.

The shared fixture data/boss/testdata/coverage_cases.json is the contract with
the page's implementation (web/static/js/coverage.js): a rule change that lands
on one side only fails the other side's test.
"""

import argparse
import csv
import glob
import io
import json
import os
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import boss_coverage  # noqa: E402
import boss_data  # noqa: E402

FIXTURE = ROOT / "data" / "boss" / "testdata" / "coverage_cases.json"
CSV_HEADER = ["map", "boss", "row", "key", "a", "b", "c", "d", "source"]
RAID_HEADER = ["key", "map_id", "name"]
CODE_HEADER = ["map", "boss", "key", "need", "level", "name", "text", "source"]


def ability(sid, name, **flags):
    row = {
        "sid": sid,
        "name": name,
        "castMs": 0,
        "a": 0,
        "b": 0,
        "c": 0,
        "phase": "",
        "target": "",
        "aura": False,
        "self": False,
        "mark": False,
        "ground": False,
        "cone": False,
        "tail": False,
        "int": False,
        "summon": 0,
        "hp": 0,
        "ok": "",
        "okNeeds": {},
    }
    row.update(flags)
    return row


def boss(entry, name, abilities=(), adds=()):
    return {
        "entry": entry,
        "name": name,
        "map": "testraid",
        "order": 1,
        "abilities": list(abilities),
        "phases": [],
        "adds": list(adds),
        "notes": [],
    }


def table(bosses, codes=()):
    return {
        "version": 1,
        "maps": [
            {
                "key": "testraid",
                "mapId": 900,
                "name": "Test Raid",
                "bosses": [item["entry"] for item in bosses],
            }
        ],
        "bosses": {str(item["entry"]): item for item in bosses},
        "codeAnswers": list(codes),
    }


def tactic(boss_entry, kind, target, spell, priority=0, param=0, comment=""):
    return {
        "map": 900,
        "boss": boss_entry,
        "kind": kind,
        "target": target,
        "spell": spell,
        "priority": priority,
        "param": param,
        "comment": comment,
    }


def rows(tactics=(), stands=(), plans=()):
    return {
        "tactics": list(tactics),
        "stands": list(stands),
        "planKillFirst": list(plans),
        "source": "migrations",
        "loadedAt": "2026-10-30T12:00:00Z",
        "files": 1,
    }


class CoverageTest(unittest.TestCase):
    def test_fixture_cases(self):
        cases = json.loads(FIXTURE.read_text(encoding="utf-8"))["cases"]
        self.assertEqual(len(cases), 33)
        for case in cases:
            with self.subTest(case=case["name"]):
                result = boss_coverage.compute(case["table"], case["rows"])
                self.assertEqual(
                    boss_coverage.project(result), case["expect"], case["name"]
                )

    def test_tactic_replay_matches_tactics_sql_test(self):
        expected = self._tactics_from_the_sql_test()
        got = {
            (row["map"], row["boss"], row["kind"], row["target"], row["spell"],
             row["priority"], row["param"])
            for row in boss_coverage.tactic_rows(ROOT)
        }
        self.assertEqual(got, expected)

    def _tactics_from_the_sql_test(self):
        """Effective rows from the existing test's parser, with its own DELETE matcher."""
        from test_boss_tactics_sql import migration_rows

        files = sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql")))
        raw = {}
        for name, map_id, boss, kind, target, spell, priority, param in migration_rows():
            raw.setdefault(name, []).append(
                (int(map_id), int(boss), kind, int(target), int(spell),
                 int(priority), float(param))
            )
        effective = {}
        for path in files:
            name = os.path.basename(path)
            text = Path(path).read_text(encoding="utf-8")
            for line in text.splitlines():
                if not line.startswith("DELETE FROM `ai_boss_tactic`"):
                    continue
                effective = {
                    key: value
                    for key, value in effective.items()
                    if not _deleted_by(line, key)
                }
            for row in raw.get(name, []):
                effective[row[:5]] = row
        return set(effective.values())

    def test_stand_replay_applies_delete(self):
        with tempfile.TemporaryDirectory() as tmp:
            columns = (
                "`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, "
                "`window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, "
                "`hold_ms`, `release_anchor`, `window_signal`, `comment`"
            )
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_boss_stand` (%s) VALUES\n"
                "(900, 90001, 1, 0, 0, 1, 'ranged', 91001, 0, 0, 0, 0, 0, 0, 0, 0, 3, 8000, 0, 'cast', 'a'),\n"
                "(900, 90001, 1, 0, 0, 1, 'healer', 91001, 0, 0, 0, 0, 0, 0, 0, 0, 2, 8000, 0, 'cast', 'b');\n"
                "DELETE FROM `ai_boss_stand` WHERE `map` = 900 AND `boss_entry` = 90001;\n"
                "INSERT INTO `ai_boss_stand` (%s) VALUES\n"
                "(900, 90001, 1, 0, 0, 1, 'ranged', 91001, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8000, 0, 'cast', 'c'),\n"
                "(900, 90001, 1, 0, 0, 1, 'ranged', 91001, 0, 1, 0, 0, 0, 0, 0, 0, 6, 8000, 0, 'cast', 'd');\n"
                % (columns, columns),
                encoding="utf-8",
            )
            stands = boss_coverage.migration_rows(tmp)["stands"]
        self.assertEqual(len(stands), 1, stands)
        self.assertEqual(stands[0]["role"], "ranged")
        self.assertEqual(stands[0]["seats"], 10)

    def test_plan_kill_first_replay_last_insert_wins(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, "
                "`kill_first`, `source`, `comment`) VALUES\n"
                "('testplan', 900, '', '', '1, 2', '', '');\n"
                "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, "
                "`kill_first`, `source`, `comment`) VALUES\n"
                "('testplan', 900, '', '', '3', '', '');\n",
                encoding="utf-8",
            )
            plans = boss_coverage.migration_rows(tmp)["planKillFirst"]
        self.assertEqual(plans, [{"plan": "testplan", "map": 900, "entries": [3]}])

    def test_plan_kill_first_update_changes_the_replayed_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `kill_first`) VALUES\n"
                "('testplan', 900, '89001');\n",
                encoding="utf-8",
            )
            before = boss_coverage.migration_rows(tmp)
            Path(tmp, "002_world.sql").write_text(
                "UPDATE `ai_dungeon_plan` SET `cc_priority` = 'ignored', `kill_first` = '77777'\n"
                "WHERE `plan` = 'testplan' AND `map` = 900;\n",
                encoding="utf-8",
            )
            after = boss_coverage.migration_rows(tmp)
        doc = table([boss(90001, "Boss One", adds=[
            {"entry": 89001, "name": "Add One", "role": "summon", "priority": 1},
        ])])

        def add_status(rows):
            return boss_coverage.compute(doc, rows)["raids"][0]["bosses"][0]["lines"][0]["status"]

        self.assertEqual(
            before["planKillFirst"], [{"plan": "testplan", "map": 900, "entries": [89001]}]
        )
        self.assertEqual(
            after["planKillFirst"], [{"plan": "testplan", "map": 900, "entries": [77777]}]
        )
        self.assertEqual(add_status(before), "partly")
        self.assertEqual(add_status(after), "missing")

    def test_boss_step_kill_lists_replay(self):
        step_columns = (
            "INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `x`, `y`, `z`, `entry`, `entries`) VALUES\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `kill_first`) VALUES\n"
                "('testplan', 900, ''), ('other', 901, '');\n"
                + step_columns
                + "('testplan', 0, 1, 'move', 1.5, 2.5, 3.5, 0, ''),\n"
                "('testplan', 0, 2, 'boss', 1.5, 2.5, 3.5, 90001, '89001, 89002'),\n"
                "('testplan', 0, 3, 'boss', 1.5, 2.5, 3.5, 90002, ''),\n"
                "('other', 0, 1, 'boss', 1.5, 2.5, 3.5, 90003, '7');\n",
                encoding="utf-8",
            )
            first = boss_coverage.migration_rows(tmp)["bossKillFirst"]
            self.assertEqual(
                first,
                [
                    {"plan": "other", "map": 901, "boss": 90003, "entries": [7]},
                    {"plan": "testplan", "map": 900, "boss": 90001, "entries": [89001, 89002]},
                ],
            )
            # A coordinate fix with an ABS filter does not touch the lists; a kind change with it does.
            Path(tmp, "002_world.sql").write_text(
                "UPDATE `ai_dungeon_step` SET `x` = 9.5 WHERE `plan` = 'testplan' AND ABS(`x` - 1.5) < 0.05;\n"
                "UPDATE `ai_dungeon_step` SET `entries` = '89002' WHERE `plan` = 'testplan' AND `leg` = 0 AND `seq` = 2;\n",
                encoding="utf-8",
            )
            second = boss_coverage.migration_rows(tmp)["bossKillFirst"]
            self.assertEqual(second[1]["entries"], [89002])
            Path(tmp, "003_world.sql").write_text(
                "UPDATE `ai_dungeon_step` SET `kind` = 'move'\n"
                "WHERE `plan` = 'testplan' AND `kind` = 'boss' AND ABS(`x` - 9.5) < 0.05;\n"
                "DELETE FROM `ai_dungeon_step` WHERE `plan` = 'other';\n",
                encoding="utf-8",
            )
            third = boss_coverage.migration_rows(tmp)["bossKillFirst"]
        self.assertEqual(third, [])

    def test_boss_step_statement_the_replay_cannot_apply_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "UPDATE `ai_dungeon_step` SET `entries` = '1' WHERE `plan` LIKE 'test%';\n",
                encoding="utf-8",
            )
            with self.assertRaises(boss_data.DataError):
                boss_coverage.migration_rows(tmp)

    def test_boss_step_kill_list_answers_a_summon(self):
        doc = table([boss(90001, "Boss One", adds=[
            {"entry": 89001, "name": "Add One", "role": "summon", "priority": 1},
        ])])
        rows = {"tactics": [], "stands": [], "planKillFirst": [], "source": "migrations",
                "bossKillFirst": [{"plan": "testplan", "map": 900, "boss": 90001, "entries": [89001]}]}
        line = boss_coverage.compute(doc, rows)["raids"][0]["bosses"][0]["lines"][0]
        self.assertEqual(line["status"], "answered")
        rows["bossKillFirst"][0]["boss"] = 90002
        line = boss_coverage.compute(doc, rows)["raids"][0]["bosses"][0]["lines"][0]
        self.assertEqual(line["status"], "partly")
        rows["bossKillFirst"] = []
        line = boss_coverage.compute(doc, rows)["raids"][0]["bosses"][0]["lines"][0]
        self.assertEqual(line["status"], "missing")

    def test_update_applies_only_to_matching_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, "
                "`spell_id`, `priority`, `param`, `comment`) VALUES\n"
                "(900, 90001, 'interrupt', 0, 91001, 0, 0, 'old'),\n"
                "(901, 90001, 'interrupt', 0, 91001, 0, 0, 'old');\n"
                "UPDATE `ai_boss_tactic` SET `priority` = 4, `comment` = 'new'\n"
                "WHERE `map` = 900 AND `spell_id` = 91001;\n",
                encoding="utf-8",
            )
            tactics = boss_coverage.migration_rows(tmp)["tactics"]
            Path(tmp, "002_world.sql").write_text(
                "INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `role`, `window_spell`, `slots`) VALUES\n"
                "(900, 90001, 'ranged', 91001, 3),\n"
                "(900, 90001, 'healer', 91001, 2);\n"
                "UPDATE `ai_boss_stand` SET `slots` = 1 WHERE `role` = 'ranged';\n",
                encoding="utf-8",
            )
            stands = boss_coverage.migration_rows(tmp)["stands"]
        by_map = {row["map"]: row for row in tactics}
        self.assertEqual((by_map[900]["priority"], by_map[900]["comment"]), (4, "new"))
        self.assertEqual((by_map[901]["priority"], by_map[901]["comment"]), (0, "old"))
        self.assertEqual({row["role"]: row["seats"] for row in stands}, {"ranged": 1, "healer": 2})

    def test_unsupported_statement_on_a_tracked_table_fails_closed(self):
        for name, statement, want in (
            ("002_world.sql",
             "UPDATE `ai_boss_stand` SET `slots` = `slots` + 1 WHERE `map` = 900;\n",
             "UPDATE `ai_boss_stand`"),
            ("003_world.sql", "DROP TABLE `ai_boss_tactic`;\n", "DROP TABLE"),
        ):
            with self.subTest(statement=statement):
                with tempfile.TemporaryDirectory() as tmp:
                    Path(tmp, "001_world.sql").write_text(
                        "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `kill_first`) VALUES\n"
                        "('testplan', 900, '1');\n",
                        encoding="utf-8",
                    )
                    Path(tmp, name).write_text(statement, encoding="utf-8")
                    with self.assertRaises(boss_data.DataError) as caught:
                        boss_coverage.migration_rows(tmp)
                message = str(caught.exception)
                self.assertIn(name, message)
                self.assertIn(want, message)

    def test_bad_kill_first_list_drops_the_plan(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `kill_first`) VALUES\n"
                "('testplan', 900, '12x');\n",
                encoding="utf-8",
            )
            plans = boss_coverage.migration_rows(tmp)["planKillFirst"]
        self.assertEqual(plans, [])

    def test_unparsable_tactic_row_fails_loudly(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "001_world.sql").write_text(
                "INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, "
                "`spell_id`, `priority`, `param`, `comment`) VALUES\n"
                "(900, 'not-an-entry', 'interrupt', 0, 91001, 0, 0, 'x');\n",
                encoding="utf-8",
            )
            with self.assertRaises(boss_data.DataError):
                boss_coverage.migration_rows(tmp)

    def test_db_rows_reads_the_three_tables(self):
        class Cursor:
            def execute(self, sql):
                self.sql = sql

            def fetchall(self):
                if "ai_boss_tactic" in self.sql:
                    return [(900, 90001, "interrupt", 0, 91001, 0, 0, "Boss One: Heal")]
                if "ai_boss_stand" in self.sql:
                    return [(900, 90001, 91001, 0, "ranged", "cast", 5)]
                if "ai_dungeon_step" in self.sql:
                    return [
                        ("testplan", 900, "move", 0, ""),
                        ("testplan", 900, "boss", 90001, "3, 4"),
                        ("testplan", 900, "boss", 90002, ""),
                        ("testplan", 900, "boss", 90003, "5x"),
                    ]
                return [("testplan", 900, "1, 2"), ("empty", 900, ""), ("bad", 900, "12x")]

        class Connection:
            def cursor(self):
                return Cursor()

            def close(self):
                pass

        rows = boss_coverage.db_rows(lambda: Connection())
        self.assertEqual(rows["tactics"][0]["spell"], 91001)
        self.assertEqual(rows["stands"][0]["seats"], 5)
        self.assertEqual(rows["planKillFirst"], [{"plan": "testplan", "map": 900, "entries": [1, 2]}])
        self.assertEqual(
            rows["bossKillFirst"],
            [{"plan": "testplan", "map": 900, "boss": 90001, "entries": [3, 4]}],
        )
        self.assertEqual(rows["source"], "world")

    def test_db_read_at_its_cap_is_refused(self):
        class Cursor:
            def execute(self, sql):
                self.sql = sql

            def fetchall(self):
                if "ai_dungeon_plan" in self.sql and "ai_dungeon_step" not in self.sql:
                    return [("p%d" % i, 900, "") for i in range(boss_coverage.ROW_CAPS["plan"] + 1)]
                return []

        class Connection:
            def cursor(self):
                return Cursor()

            def close(self):
                pass

        with self.assertRaises(boss_data.DataError):
            boss_coverage.db_rows(lambda: Connection())

    def test_db_missing_table_is_empty(self):
        class Error(Exception):
            pass

        class Cursor:
            def execute(self, sql):
                self.sql = sql

            def fetchall(self):
                if "ai_boss_stand" in self.sql:
                    raise Error(1146, "Table 'tw_world.ai_boss_stand' doesn't exist")
                if "ai_boss_tactic" in self.sql:
                    return [(900, 90001, "interrupt", 0, 91001, 0, 0, "")]
                return []

        class Connection:
            def cursor(self):
                return Cursor()

            def close(self):
                pass

        rows = boss_coverage.db_rows(lambda: Connection())
        self.assertEqual(len(rows["tactics"]), 1)
        self.assertEqual(rows["stands"], [])

    def test_no_boss_ids_in_code(self):
        text = (ROOT / "tools" / "boss_coverage.py").read_text(encoding="utf-8")
        allowed = {"1146", "1000", "5000", "20000", "358", "390", "500"}
        found = [
            match.group(0)
            for match in re.finditer(r"\b\d{4,5}\b", text)
            if match.group(0) not in allowed
        ]
        self.assertEqual(found, [], "boss entry or spell id written in code")


def _deleted_by(statement, key):
    """The DELETE condition of the existing SQL test's statement, evaluated on a key."""
    columns = {"map": 0, "boss_entry": 1, "kind": 2, "target_entry": 3, "spell_id": 4}
    condition = statement.split(" WHERE ", 1)[1].rstrip(";")
    for part in re.split(r"\s+AND\s+", condition):
        match = re.fullmatch(r"`(\w+)` = ('[^']*'|\d+)", part)
        if match:
            column, raw = match.groups()
            value = raw[1:-1] if raw.startswith("'") else int(raw)
            if key[columns[column]] != value:
                return False
            continue
        match = re.fullmatch(r"`(\w+)` IN \(([^)]*)\)", part)
        if not match:
            raise AssertionError("unparsed WHERE %r" % part)
        column, raw_values = match.groups()
        values = [int(item.strip()) for item in raw_values.split(",")]
        if key[columns[column]] not in values:
            return False
    return True


def text_table():
    return table(
        [
            boss(
                90001,
                "Boss One",
                [
                    ability(91001, "Spell A", int=True),
                    ability(91002, "Spell B", self=True),
                    ability(91003, "Spell C"),
                ],
            )
        ]
    )


def text_rows():
    return rows(
        tactics=[
            tactic(90001, "interrupt", 0, 91001, comment="Boss One: Heal"),
            tactic(90001, "interrupt", 0, 91002, comment="Boss One: Heal"),
            tactic(90001, "behind", 0, 91099, param=10),
        ]
    )


class CliFixture:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.csv = self.dir / "boss_abilities.csv"
        self.names = self.dir / "names.json"
        self.raids = self.dir / "raids.csv"
        self.answers = self.dir / "code_answers.csv"
        self.sql = self.dir / "sql"
        self.sql.mkdir()
        with open(self.csv, "w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle, lineterminator="\n")
            writer.writerow(CSV_HEADER)
            writer.writerow(["testraid", "90001", "ability", "91001", "0", "0", "0",
                             "t=aoe", "src/boss.cpp:1"])
            writer.writerow(["testraid", "90001", "ability", "91002", "0", "0", "0",
                             "int", "src/boss.cpp:2"])
        self.names.write_text(
            json.dumps(
                {
                    "spells": {
                        "91001": {"name": "Spell A"},
                        "91002": {"name": "Spell B"},
                    },
                    "creatures": {"90001": {"name": "Boss One"}},
                }
            ),
            encoding="utf-8",
        )
        with open(self.raids, "w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle, lineterminator="\n")
            writer.writerow(RAID_HEADER)
            writer.writerow(["testraid", "900", "Test Raid"])
        with open(self.answers, "w", newline="", encoding="utf-8") as handle:
            csv.writer(handle, lineterminator="\n").writerow(CODE_HEADER)
        (self.sql / "001_world.sql").write_text(
            "INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, "
            "`spell_id`, `priority`, `param`, `comment`) VALUES\n"
            "(900, 90001, 'interrupt', 0, 91001, 0, 0, 'Boss One: Heal');\n",
            encoding="utf-8",
        )

    def args(self, **over):
        values = {
            "raid": "",
            "boss": 0,
            "all": False,
            "json": False,
            "fail_on_missing": False,
            "csv": str(self.csv),
            "names": str(self.names),
            "raids": str(self.raids),
            "code_answers": str(self.answers),
            "sql_dir": str(self.sql),
            "repo_root": str(self.dir),
            "core_dir": str(self.dir),
            "kb_dir": str(self.dir),
            "db": False,
            "db_host": "x",
            "db_user": "x",
            "db_pass": "x",
            "db_name": "x",
            "db_defaults": None,
        }
        values.update(over)
        return argparse.Namespace(**values)

    def table(self):
        return boss_data.build_document(
            boss_data.read_rows(self.csv),
            boss_data.read_names(self.names),
            boss_data.read_raids(self.raids),
            boss_data.read_code_answers(self.answers),
            self.dir,
        )


class CliTest(unittest.TestCase):
    def make(self):
        fixture = CliFixture()
        self.addCleanup(fixture.tmp.cleanup)
        return fixture

    def test_cli_text_format(self):
        result = boss_coverage.compute(text_table(), text_rows())
        expected = (
            "Test Raid (map 900): 3 lines: 1 answered, 1 partly, 1 missing, 0 not needed"
            "   [rows: migrations, 1 file]\n"
            "Boss One (90001): 1 answered, 1 partly, 1 missing, 0 not needed\n"
            "  partly   91002  Spell B                interrupt: interrupt row (Boss One: Heal); "
            "purge: no row\n"
            "  missing  91003  Spell C                classify: the table does not say what it "
            "needs (add a need flag or ok=<reason>)\n"
            "  row      behind 91099                   no ability with spell 91099 in the table\n"
            "  warning  interrupt row on 91002 Spell B: the table does not flag it int; check "
            "that the core can interrupt it\n"
        )
        self.assertEqual(boss_coverage.report_text(result), expected)

    def test_cli_unknown_raid_exit_2(self):
        fixture = self.make()
        err = io.StringIO()
        with redirect_stderr(err):
            code = boss_coverage.cmd_coverage(fixture.args(raid="nope"))
        self.assertEqual(code, 2)
        self.assertIn("no raid nope; known: testraid", err.getvalue())

    def test_cli_fail_on_missing_exit_1(self):
        fixture = self.make()
        with redirect_stdout(io.StringIO()):
            code = boss_coverage.cmd_coverage(fixture.args(fail_on_missing=True))
        self.assertEqual(code, 1)

    def test_cli_accepts_map_id(self):
        fixture = self.make()
        out = io.StringIO()
        with redirect_stdout(out):
            code = boss_coverage.cmd_coverage(fixture.args(raid="900"))
        self.assertEqual(code, 0)
        self.assertTrue(out.getvalue().startswith("Test Raid (map 900):"), out.getvalue())

    def test_cli_json_is_compute_result(self):
        fixture = self.make()
        args = fixture.args(json=True)
        out = io.StringIO()
        with redirect_stdout(out):
            code = boss_coverage.cmd_coverage(args)
        self.assertEqual(code, 0)
        expected = boss_coverage.compute(fixture.table(), boss_coverage.migration_rows(args.sql_dir))
        got = json.loads(out.getvalue())
        got.pop("loadedAt", None)
        expected.pop("loadedAt", None)
        self.assertEqual(got, expected)

    def test_cli_fails_on_an_unsupported_tracked_statement(self):
        fixture = self.make()
        (fixture.sql / "002_world.sql").write_text(
            "UPDATE `ai_dungeon_plan` SET `kill_first` = '1';\n", encoding="utf-8"
        )
        err = io.StringIO()
        with redirect_stderr(err):
            code = boss_coverage.cmd_coverage(fixture.args())
        self.assertEqual(code, 2)
        self.assertIn("002_world.sql", err.getvalue())
        self.assertIn("UPDATE `ai_dungeon_plan`", err.getvalue())

    def test_cli_runs_on_every_committed_raid(self):
        raids = boss_data.read_raids(ROOT / "data" / "boss" / "raids.csv")
        out = io.StringIO()
        with redirect_stdout(out):
            code = boss_data.main(["coverage"])
        self.assertEqual(code, 0)
        text = out.getvalue()
        for raid in raids:
            header = f"{raid['name']} (map {raid['mapId']}): "
            self.assertIn(header, text, raid["key"])


if __name__ == "__main__":
    unittest.main()
