"""Gate tests for tools/boss_data.py and the committed boss table.

Covers the four failing cases the fd-boss spec names for the data task:
a timer that does not match its cited line, a duplicate spell/add entry, a CSV
edit without a rebuilt JSON, and the --db existence check. The fixture scripts
are tiny fake C++ files so the citation checks run without a core checkout.
"""

import argparse
import csv
import json
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import boss_data  # noqa: E402

CORE = [
    "#define SPELL_A 24328",
    "#define NPC_ADD 14884",
    "void Reset() { A_Timer = 15000; }",
    "void UpdateAI() { A_Timer = urand(14000, 16000); }",
    "SummonCreature(NPC_ADD, x);",
    "if (GetHealthPercent() < 50) m_bPhaseTwo = true;",
]

NAMES = {
    "spells": {"24328": {"name": "Corrupted Blood", "castMs": 0}},
    "creatures": {"14517": {"name": "High Priestess Jeklik"}, "14884": {"name": "Parasitic Serpent"}},
}

HEADER = ["map", "boss", "row", "key", "a", "b", "c", "d", "source"]
RAID_HEADER = ["key", "map_id", "name"]
CODE_HEADER = ["map", "boss", "key", "need", "level", "name", "text", "source"]


def ability(**kw):
    row = {
        "map": "zulgurub",
        "boss": "14517",
        "row": "ability",
        "key": "24328",
        "a": "15000",
        "b": "14000",
        "c": "16000",
        "d": "t=random",
        "source": "boss.cpp:1;boss.cpp:3;boss.cpp:4",
    }
    row.update(kw)
    return row


def phase(name, rule, order, source="boss.cpp:6"):
    return {
        "map": "zulgurub",
        "boss": "14517",
        "row": "phase",
        "key": name,
        "a": rule,
        "b": str(order),
        "c": "",
        "d": "",
        "source": source,
    }


def add(**kw):
    row = {
        "map": "zulgurub",
        "boss": "14517",
        "row": "add",
        "key": "14884",
        "a": "summon",
        "b": "0",
        "c": "",
        "d": "",
        "source": "boss.cpp:2;boss.cpp:5",
    }
    row.update(kw)
    return row


BASE_ROWS = [
    ability(),
    add(),
    phase("snake", "hp<50", 1),
    phase("p1", "start", 2, source="boss.cpp:3"),
]


def answer(**kw):
    row = {
        "map": "zulgurub",
        "boss": "14517",
        "key": "24328",
        "need": "dispel",
        "level": "full",
        "name": "dragon flank",
        "text": "test answer",
        "source": "code/flank.cpp",
    }
    row.update(kw)
    return [row[c] for c in CODE_HEADER]


class Fixture:
    def __init__(self, rows=None, names=None, raids=None, answers=None):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.csv = self.dir / "boss_abilities.csv"
        self.json = self.dir / "boss_abilities.json"
        self.names = self.dir / "names.json"
        self.raids = self.dir / "raids.csv"
        self.answers = self.dir / "code_answers.csv"
        (self.dir / "core").mkdir()
        (self.dir / "core" / "boss.cpp").write_text("\n".join(CORE) + "\n")
        self.write(rows if rows is not None else [dict(r) for r in BASE_ROWS],
                   names if names is not None else NAMES)
        self.write_raids(raids if raids is not None else [["zulgurub", "309", "Zul'Gurub"]])
        self.write_answers(answers if answers is not None else [])

    def repo_file(self, path, text):
        """A file the code answers can cite, under the fixture's repo root."""
        target = self.dir / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text + "\n")
        return path

    def write_answers(self, answers):
        with open(self.answers, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f, lineterminator="\n")
            writer.writerow(CODE_HEADER)
            for row in answers:
                writer.writerow(row)
        self.answer_rows = answers

    def write_raids(self, raids):
        with open(self.raids, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f, lineterminator="\n")
            writer.writerow(RAID_HEADER)
            for row in raids:
                writer.writerow(row)
        self.raid_rows = raids

    def write(self, rows, names=None):
        with open(self.csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f, lineterminator="\n")
            writer.writerow(HEADER)
            for row in rows:
                writer.writerow([row[c] for c in HEADER])
        self.names.write_text(json.dumps(names or NAMES))
        self.rows = rows

    def rebuild(self):
        doc = boss_data.build_document(
            boss_data.read_rows(self.csv),
            boss_data.read_names(self.names),
            boss_data.read_raids(self.raids),
            boss_data.read_code_answers(self.answers),
            self.dir,
        )
        self.json.write_text(boss_data.dump_json(doc))

    def args(self, db=False):
        return argparse.Namespace(
            csv=str(self.csv),
            json=str(self.json),
            names=str(self.names),
            raids=str(self.raids),
            code_answers=str(self.answers),
            repo_root=str(self.dir),
            core_dir=str(self.dir / "core"),
            kb_dir=str(self.dir / "kb"),
            db=db,
            db_host="x",
            db_user="x",
            db_pass="x",
            db_name="x",
            db_defaults=None,
        )

    def validate(self, db=False):
        return boss_data.validate(self.args(db))


class FakeCursor:
    def __init__(self, spells, creatures, effects, facts, targets):
        self.spells = spells
        self.creatures = creatures
        self.effects = effects
        self.facts = facts
        self.targets = targets

    def execute(self, sql, params=()):
        self.sql = sql
        self.params = params

    def fetchall(self):
        if "effectImplicitTargetA1" in self.sql:
            return [(sid, *self.targets[sid]) for sid in self.params if sid in self.targets]
        if "preventionType" in self.sql:
            return [(sid, *self.facts[sid]) for sid in self.params if sid in self.facts]
        if "effect1" in self.sql:
            return [(sid, *self.effects[sid]) for sid in self.params if sid in self.effects]
        if "spell_template" in self.sql:
            return [(sid,) for sid in self.params if sid in self.spells]
        return [(entry,) for entry in self.params if entry in self.creatures]


class FakeConn:
    def __init__(self, spells, creatures, effects=None, facts=None, targets=None):
        self.cursor_obj = FakeCursor(
            set(spells), set(creatures), effects or {}, facts or {}, targets or {}
        )

    def cursor(self):
        return self.cursor_obj

    def close(self):
        pass


class BossDataTest(unittest.TestCase):
    def make(self, rows=None, names=None, raids=None, answers=None):
        fx = Fixture(rows, names, raids, answers)
        self.addCleanup(fx.tmp.cleanup)
        return fx

    def test_valid_fixture_passes(self):
        fx = self.make()
        fx.rebuild()
        errors, notes = fx.validate()
        self.assertEqual(errors, [])
        self.assertIn("db check skipped", notes)

    def test_timer_must_match_its_cited_line(self):
        # Spec failing case 1: a=15000 citing a line that says 14000.
        rows = [ability(a="14000", source="boss.cpp:1;boss.cpp:3"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertTrue(any("a=14000" in e for e in errors), errors)

    def test_spell_id_must_be_cited(self):
        rows = [ability(source="boss.cpp:3;boss.cpp:4"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertTrue(any("do not define spell 24328" in e for e in errors), errors)

    def test_duplicate_ability_and_add_fail(self):
        # Spec failing case 2: duplicate spell id or add entry per boss.
        fx = self.make([ability(), ability(source="boss.cpp:1"), add()])
        errors, _ = fx.validate()
        self.assertTrue(any("duplicate ability 24328" in e for e in errors), errors)

        fx = self.make([ability(), add(), add(source="boss.cpp:2")])
        errors, _ = fx.validate()
        self.assertTrue(any("duplicate add 14884" in e for e in errors), errors)

    def test_summon_flag_needs_an_add_row(self):
        rows = [ability(d="t=random;summon=14884"),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("summons 14884 with no add row" in e for e in errors), errors)

    def test_start_rule_must_be_last(self):
        rows = [ability(), add(),
                phase("p1", "start", 1, source="boss.cpp:3"),
                phase("snake", "hp<50", 2)]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("start rule must be the last" in e for e in errors), errors)

    def test_cone_and_tail_flags_parse(self):
        flags = boss_data.parse_flags("t=aoe;cone;tail")
        self.assertTrue(flags["cone"])
        self.assertTrue(flags["tail"])
        self.assertEqual(sorted(boss_data.derived_needs(flags)), ["front", "tail"])
        plain = boss_data.parse_flags("t=aoe")
        self.assertFalse(plain["cone"])
        self.assertFalse(plain["tail"])
        self.assertEqual(boss_data.derived_needs(plain), [])

    def test_unknown_ok_reason_fails(self):
        rows = [ability(d="t=random;ok=banana"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("unknown ok reason 'banana'" in e for e in errors), errors)

    def test_ok_need_not_derived_fails(self):
        # ok.<need> on a row whose flags derive no such need is a typo, not a statement.
        rows = [ability(d="t=random;ok.avoid=heal"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("do not derive avoid" in e for e in errors), errors)

    def test_ok_and_ok_need_together_fail(self):
        rows = [ability(d="t=victim;int;ok=tank;ok.interrupt=heal"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("cannot be combined" in e for e in errors), errors)

    def test_nokick_outside_interrupt_fails(self):
        rows = [ability(d="t=random;aura;ok.dispel=nokick"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("only valid as ok.interrupt" in e for e in errors), errors)

    def test_tank_needs_victim_target(self):
        rows = [ability(d="t=random;ok=tank"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("needs a t=victim row" in e for e in errors), errors)

    def test_phase_reason_needs_phase_aura_or_hp(self):
        rows = [ability(d="t=random;ok=phase"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("ok reason phase" in e for e in errors), errors)

        # hp<N on the row itself is enough.
        rows = [ability(d="t=random;ok=phase;hp30"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertFalse(any("ok reason phase" in e for e in errors), errors)

        # The spell being the boss's own aura= phase rule is the other way in.
        rows = [ability(d="t=random;ok=phase"), add(),
                phase("swarm", "aura=24328", 1), phase("p1", "start", 2, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertFalse(any("ok reason phase" in e for e in errors), errors)

    def test_raids_csv_covers_every_map(self):
        # A map slug in the CSV without a raids.csv row cannot be shown as a raid.
        rows = [dict(r) for r in BASE_ROWS]
        for row in rows:
            row["map"] = "onyxia"
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("has no row in raids.csv" in e for e in errors), errors)

    def test_raid_without_boss_fails(self):
        # A raids.csv row whose key has no boss in the CSV would render an empty raid.
        fx = self.make(raids=[["zulgurub", "309", "Zul'Gurub"], ["naxx", "533", "Naxxramas"]])
        errors, _ = fx.validate()
        self.assertTrue(any("has no boss in the CSV" in e for e in errors), errors)

    def code_fixture(self, answers, rows=None):
        fx = self.make(rows)
        fx.repo_file("code/flank.cpp", '#define NPC_TEST 14517\nNextAction("dragon flank", 40);')
        fx.write_answers(answers)
        return fx

    def test_code_answer_path_must_exist(self):
        fx = self.code_fixture([answer(source="code/gone.cpp")])
        errors, _ = fx.validate()
        self.assertTrue(any("code/gone.cpp does not exist" in e for e in errors), errors)

    def test_code_answer_name_must_be_in_cited_files(self):
        fx = self.code_fixture([answer()])
        fx.repo_file("code/flank.cpp", "#define NPC_TEST 14517\n// no action registered")
        errors, _ = fx.validate()
        self.assertTrue(any('do not register "dragon flank"' in e for e in errors), errors)

    def test_code_answer_entry_must_be_in_cited_files(self):
        fx = self.code_fixture([answer()])
        fx.repo_file("code/flank.cpp", 'NextAction("dragon flank", 40);')
        errors, _ = fx.validate()
        self.assertTrue(any("do not name creature 14517" in e for e in errors), errors)

        # A constant that holds the entry counts, like the citation checks.
        fx.repo_file("code/flank.cpp", '#define NPC_TEST 14517\nNextAction("dragon flank", 40);')
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertFalse(any("do not name creature" in e for e in errors), errors)

    def test_code_answer_spell_must_be_an_ability_of_the_boss(self):
        fx = self.code_fixture([answer(key="99999")])
        errors, _ = fx.validate()
        self.assertTrue(any("spell 99999 is no ability" in e for e in errors), errors)

        fx = self.code_fixture([answer(key="24328")])
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertFalse(any("is no ability" in e for e in errors), errors)

    def test_code_answer_add_must_be_an_add_row(self):
        fx = self.code_fixture([answer(key="add=14884", need="kill")])
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertFalse(any("is no add row" in e for e in errors), errors)

        fx = self.code_fixture([answer(key="add=12345", need="kill")])
        errors, _ = fx.validate()
        self.assertTrue(any("12345 is no add row" in e for e in errors), errors)

    def test_star_code_answer_must_be_partly(self):
        star = dict(map="*", boss="*", key="flag=ground", need="avoid")
        fx = self.code_fixture([answer(level="full", **star)])
        errors, _ = fx.validate()
        self.assertTrue(any("a * row must be partly" in e for e in errors), errors)

        fx = self.code_fixture([answer(level="partly", **star)])
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertEqual(errors, [])

    def test_duplicate_code_answer_fails(self):
        fx = self.code_fixture([answer(), answer()])
        errors, _ = fx.validate()
        self.assertTrue(any("duplicate code answer" in e for e in errors), errors)

    def test_committed_code_answers_pass_check(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        answers = boss_data.read_code_answers(boss_data.DEFAULT_CODE_ANSWERS)
        map_doc, bosses = boss_data.make_bosses(
            rows, names, boss_data.read_raids(boss_data.DEFAULT_RAIDS)
        )
        errors = list(boss_data.check_code_answers(answers, map_doc, bosses, boss_data.ROOT))
        self.assertEqual(errors, [])
        self.assertTrue(answers)

    def test_stale_json_fails(self):
        # Spec failing case 3: the CSV was edited and the JSON was not rebuilt.
        fx = self.make()
        fx.rebuild()
        rows = [dict(r) for r in BASE_ROWS]
        rows[0]["d"] = "t=victim"
        fx.write(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("differs from a fresh build" in e for e in errors), errors)

    def test_missing_json_fails(self):
        fx = self.make()
        errors, _ = fx.validate()
        self.assertTrue(any("committed JSON not found" in e for e in errors), errors)

    def test_citation_past_end_of_file_fails(self):
        rows = [ability(source="boss.cpp:1;boss.cpp:3;boss.cpp:4;boss.cpp:99")]
        fx = self.make(rows)
        fx.rebuild()
        errors, _ = fx.validate()
        self.assertTrue(any("past end of file" in e for e in errors), errors)

    def test_db_check_reports_missing_ids(self):
        # Spec failing case 4: with --db every id must exist in the world DB.
        fx = self.make()
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(spells=set(), creatures={14884})
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(any("spell 24328 is not in spell_template" in e for e in errors), errors)

    def test_db_check_reports_missing_creature(self):
        fx = self.make()
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(spells={24328}, creatures=set())
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(any("creature 14884 is not in creature_template" in e for e in errors), errors)

    def test_ground_flag_needs_persistent_area_effect(self):
        rows = [ability(d="t=random;ground"), add(), phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, effects={24328: (2, 6, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(any("no persistent-area effect" in e for e in errors), errors)

        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, effects={24328: (27, 0, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertFalse(any("persistent-area" in e for e in errors), errors)

    def test_db_nokick_on_kickable_spell_fails(self):
        rows = [ability(d="t=victim;int;ok.interrupt=nokick"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, facts={24328: (1, 0x02, 14, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(any("spell 24328 is kickable" in e for e in errors), errors)

        # preventionType 0 is the core's own "the kick cannot stop it".
        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, facts={24328: (0, 0x02, 14, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertFalse(any("is kickable" in e for e in errors), errors)

    def test_db_nodispel_on_dispellable_spell_fails(self):
        rows = [ability(d="t=random;aura;ok.dispel=nodispel"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, facts={24328: (0, 0, 0, 8)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(any("spell 24328 can be dispelled" in e for e in errors), errors)

    def test_db_cone_without_target_24_fails(self):
        rows = [ability(d="t=aoe;cone"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        fx.rebuild()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, targets={24328: (1, 0, 0, 0, 0, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertTrue(
            any("flagged cone but has no cone target (24)" in e for e in errors), errors
        )

        boss_data.db_connect = lambda args: FakeConn(
            spells={24328}, creatures={14884}, targets={24328: (24, 0, 0, 0, 0, 0)}
        )
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertFalse(any("cone target" in e for e in errors), errors)

    def test_committed_data_builds_to_committed_json(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        committed = boss_data.DEFAULT_JSON.read_text(encoding="utf-8")
        self.assertEqual(committed, boss_data.dump_json(doc))
        self.assertEqual(len(doc["bosses"]), 22)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 138)

    def test_every_panel_name_is_present(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        for boss in doc["bosses"].values():
            self.assertTrue(boss["name"])
            for entry in boss["abilities"] + boss["adds"]:
                self.assertTrue(entry["name"], entry)
            for phase in boss["phases"]:
                self.assertIn(phase["rule"]["kind"], ("hp", "aura", "flag", "start"))


# AQ40's last three encounters, written off the cited core lines (not from the CSV):
# boss -> (phases, {spell: (a, b, c, flags, castMs)}, adds)
AQ40_REST = {
    15511: ([], {
        3391: (0, 2000, 8000, "t=victim;ok=tank", 0),
        19983: (0, 5000, 12000, "t=victim", 0),
        25812: (0, 8000, 14000, "t=aoe;aura", 0),
        25786: (0, 0, 0, "t=aoe;ground", 0),
    }, []),
    15543: ([], {
        19408: (0, 20000, 20000, "t=aoe;aura", 0),
        25807: (0, 12000, 12000, "t=ally;int", 2000),
        24213: (0, 12000, 20000, "t=victim;ok=tank", 0),
    }, [(15621, "summon", 1)]),
    15544: ([], {
        26561: (0, 15000, 20000, "t=random", 0),
        18813: (0, 10000, 14000, "t=victim", 0),
        19128: (0, 15000, 20000, "t=victim", 0),
        25790: (0, 0, 0, "t=self;self;ok.purge=nopurge", 0),
    }, []),
    15299: (["aura=25937", "start"], {
        25993: (0, 7000, 12000, "p=fight;t=aoe;cone", 0),
        25991: (0, 10000, 15000, "p=fight;t=aoe;aura", 0),
        25989: (0, 0, 0, "t=aoe;ground", 0),
        25994: (0, 0, 0, "t=self;self;ok.purge=nopurge", 0),
        25937: (0, 0, 0, "t=self;self;ok=phase", 0),
        25938: (0, 0, 0, "t=self;ok=script", 0),
    }, [(15667, "summon", 1), (15922, "trigger", 0)]),
    15517: (["aura=26615", "start"], {
        26103: (0, 15000, 15000, "t=aoe;int;ok.interrupt=nokick", 1500),
        26102: (0, 12000, 17000, "t=aoe;int;ok.interrupt=nokick", 2000),
        26616: (0, 0, 0, "p=enraged;t=random;int;ok.interrupt=nokick", 2000),
        26615: (0, 0, 0, "t=self;self;hp20;ok=phase", 0),
        26063: (0, 0, 0, "t=self;self;ok.purge=nopurge", 1500),
        26058: (0, 0, 0, "t=self;summon=15712", 0),
        26617: (0, 10000, 10000, "p=enraged;t=self", 0),
        26100: (0, 0, 0, "t=aoe", 0),
        26262: (0, 0, 0, "t=self;ok=script", 250),
    }, [(15712, "summon", 1), (15717, "trigger", 0), (15718, "summon", 2)]),
}


def _ability_flag_text(ability):
    """The CSV flag spelling of a built ability, so a fixture can pin every flag."""
    parts = []
    if ability["phase"]:
        parts.append("p=" + ability["phase"])
    if ability["target"]:
        parts.append("t=" + ability["target"])
    for name in ("aura", "self", "mark", "ground", "cone", "tail", "int"):
        if ability[name]:
            parts.append(name)
    if ability["summon"]:
        parts.append("summon=%d" % ability["summon"])
    if ability["hp"]:
        parts.append("hp%d" % ability["hp"])
    if ability["ok"]:
        parts.append("ok=" + ability["ok"])
    for need, reason in ability["okNeeds"].items():
        parts.append("ok.%s=%s" % (need, reason))
    return ";".join(parts)


def _phase_text(phase):
    rule = phase["rule"]
    if rule["kind"] == "start":
        return "start"
    if rule["kind"] == "aura":
        return "aura=%d" % rule["aura"]
    if rule["kind"] == "hp":
        return "hp<%d" % rule["hp"]
    return "flag=%s" % rule["flag"]


class Aq40RestValueTest(unittest.TestCase):
    """The Bug Trio, Viscidus and Ouro rows, pinned to the numbers read in the core scripts."""

    @classmethod
    def setUpClass(cls):
        cls.doc = boss_data.build_document(
            boss_data.read_rows(boss_data.DEFAULT_CSV),
            boss_data.read_names(boss_data.DEFAULT_NAMES),
        )

    def boss(self, entry):
        self.assertIn(str(entry), self.doc["bosses"], "boss %d is missing" % entry)
        return self.doc["bosses"][str(entry)]

    def test_every_ability_phase_and_add(self):
        for entry, (phases, abilities, adds) in AQ40_REST.items():
            boss = self.boss(entry)
            self.assertEqual([_phase_text(p) for p in boss["phases"]], phases, entry)
            got = {a["sid"]: a for a in boss["abilities"]}
            self.assertEqual(set(got), set(abilities), "ability set of %d" % entry)
            for sid, (a, b, c, flags, cast_ms) in abilities.items():
                ability = got[sid]
                self.assertEqual(
                    (ability["a"], ability["b"], ability["c"]), (a, b, c),
                    "%d/%d timers" % (entry, sid),
                )
                self.assertEqual(ability["castMs"], cast_ms, "%d/%d cast ms" % (entry, sid))
                self.assertEqual(_ability_flag_text(ability), flags, "%d/%d flags" % (entry, sid))
            self.assertEqual(
                [(x["entry"], x["role"], x["priority"]) for x in boss["adds"]], adds,
                "adds of %d" % entry,
            )

    def test_the_trio_has_no_phase_rows(self):
        # No aura or flag carries a trio state, so a phase row would be a guess the panel then latches.
        for entry in (15511, 15543, 15544):
            self.assertEqual(self.boss(entry)["phases"], [])

    def test_viscidus_freeze_is_the_only_visible_phase(self):
        boss = self.boss(15299)
        self.assertEqual(len(boss["phases"]), 2)
        self.assertEqual(boss["phases"][0]["rule"], {"kind": "aura", "aura": 25937})
        # The exploded state is SPAWNING + feign death + invisible, none of it in uf: a note, not a rule.
        self.assertFalse(any(p["rule"]["kind"] == "flag" for p in boss["phases"]))
        self.assertTrue(any("shatter" in note for note in boss["notes"]))
        self.assertTrue(all(a["phase"] == "" for a in boss["abilities"] if a["sid"] == 25938))
        self.assertTrue(all(a["phase"] == "fight" for a in boss["abilities"] if a["sid"] in (25993, 25991)))
        abilities = {a["sid"]: a for a in boss["abilities"]}
        self.assertTrue(abilities[25993]["cone"], "Poison Shock is a front cone")
        self.assertFalse(abilities[25991]["cone"], "Poison Bolt Volley is a circle, not a cone")

    def test_ouro_enrage_is_the_only_visible_phase(self):
        boss = self.boss(15517)
        self.assertEqual([_phase_text(p) for p in boss["phases"]], ["aura=26615", "start"])
        abilities = {a["sid"]: a for a in boss["abilities"]}
        self.assertEqual(abilities[26616]["phase"], "enraged")
        self.assertEqual(abilities[26617]["phase"], "enraged")
        self.assertEqual(abilities[26058]["summon"], 15712)
        # The submerge aura reaches telemetry only as Ouro turns invisible, so a phase row would vanish
        # with him; the row stays as a self aura and the panel note carries the 30 s.
        self.assertEqual(abilities[26063]["phase"], "")
        self.assertEqual(abilities[26063]["okNeeds"], {"purge": "nopurge"})
        self.assertTrue(any("leaves the panel" in note for note in boss["notes"]))
        # Sweep, Sand Blast and Boulder are casts the core's kick cannot stop: PreventionType is SILENCE
        # for Sand Blast but its interruptFlags lack 0x02, and the other two are not SILENCE at all. The
        # cone flag stays off Sand Blast because the table's DB contract only accepts implicit target 24
        # and this one is a target-54 cone (its shape row is pinned by tools/test_spell_shapes.cpp).
        for sid in (26103, 26102, 26616):
            self.assertTrue(abilities[sid]["int"], sid)
            self.assertEqual(abilities[sid]["okNeeds"], {"interrupt": "nokick"}, sid)
        self.assertTrue(abilities[26103]["int"] and not abilities[26103]["cone"])
        self.assertTrue(abilities[26102]["int"] and not abilities[26102]["cone"])
        # Yauj's Great Heal is the one Ouro-era great heal that stays a real interrupt.
        yauj = {a["sid"]: a for a in self.boss(15543)["abilities"]}
        self.assertEqual(yauj[25807]["okNeeds"], {})

    def test_trio_and_ouro_adds_are_named(self):
        self.assertEqual([a["name"] for a in self.boss(15543)["adds"]], ["Yauj Brood"])
        self.assertEqual(
            [a["name"] for a in self.boss(15517)["adds"]],
            ["Dirt Mound", "Ouro Trigger", "Ouro Scarab"],
        )
        for entry in (15511, 15543, 15544):
            self.assertTrue(any("devour" in note for note in self.boss(entry)["notes"]), entry)


# ---------------------------------------------------------------------------
# Every raid against the core's spell data.
#
# The CSV is the panel's source; the core's spell_template is the game's. These
# checks read the same rows and ask the data the questions the code relies on:
# a phase aura must reach telemetry, a spell the panel draws a shape for must be
# a cast, `int` must mean the core's own kick rule, and `self` must be an aura.
# Without a core checkout the class skips; the checks above still run.

CORE_DIR = Path(os.environ.get("TORTOISE_CORE_DIR", Path.home() / "tortoise-wow"))
CORE_SPELL_SQL = CORE_DIR / "sql" / "base" / "tw_world_spell_template.sql"

SPELL_SOURCE_COLUMNS = {
    "entry", "attributes", "preventionType", "interruptFlags", "castingTimeIndex",
    "effect1", "effect2", "effect3",
    "effectApplyAuraName1", "effectApplyAuraName2", "effectApplyAuraName3",
}
SPELL_MOD_COLUMNS = {"Id", "Attributes", "CastingTimeIndex", "InterruptFlags"}

# The core's SpellEntry::IsEffectAppliesAura set (6, 35, 119, 128, 129, 132, 133,
# 134) plus PERSISTENT_AREA_AURA 27, which boss_data.py's ground check already
# treats as an aura. Effect 65 does not exist in this core's SpellDefines.h.
AURA_EFFECTS = {6, 27, 35, 119, 128, 129, 132, 133, 134}

# Rows that predate the source check: they are not AQ40's to change here. Each
# entry is a known disagreement between the CSV and the core data, kept visible
# so the check still passes and a later data pass can clear it. The value is the
# reason, printed when the entry stops reproducing so it cannot rot silently.
EXPECTED_SOURCE_PROBLEMS = {
    # Shape rows whose spells are instant: the panel draws the area from
    # runtime/SpellShapeData.h but cannot time a cast bar, which the shape test
    # predates. Not changed by the AQ40 data PR.
    ("shape-instant", 15847): "Onyxia Tail Sweep, an instant back cone",
    ("shape-instant", 19272): "AQ40 Lava Breath, an instant target-54 cone",
    ("shape-instant", 24839): "Acid Breath, an instant target-24 cone",
    ("shape-instant", 25814): "AQ40 Wide Slash, an instant target-24 cone",
    ("shape-instant", 26029): "AQ40 Dark Glare, an instant 15 degree cone",
    # Zul'Gurub rows marked nokick although preventionType/InterruptFlags look
    # kickable: both are instant channels (castingTimeIndex 1), so the core's
    # generic-cast branch never sees a cast, but their channel flags may still
    # let a kick through. Pre-existing ZG data, worth a separate fix.
    ("nokick-kickable", 23953): "Jeklik Mind Flay, instant channel",
    ("nokick-kickable", 24300): "Mar'li Drain Life, instant channel",
    # Jeklik's self Great Heal is cast on himself but applies no aura: the
    # `self` flag overstates it. Pre-existing ZG data.
    ("self-no-aura", 23954): "Jeklik Great Heal heals himself, no aura",
}


def _sql_statements(text):
    """Split a dump into statements; quotes, escapes and comments respected."""
    statements, buffer = [], []
    quote = None
    escaped = False
    i, size = 0, len(text)
    while i < size:
        char = text[i]
        if quote:
            buffer.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            i += 1
            continue
        if char == "-" and text.startswith("--", i):
            end = text.find("\n", i)
            i = size if end < 0 else end + 1
            continue
        if char == "/" and text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = size if end < 0 else end + 2
            continue
        if char in "'\"":
            quote = char
            buffer.append(char)
        elif char == ";":
            statement = "".join(buffer).strip()
            if statement:
                statements.append(statement)
            buffer = []
        else:
            buffer.append(char)
        i += 1
    statement = "".join(buffer).strip()
    if statement:
        statements.append(statement)
    return statements


def _create_columns(text, table, path):
    start = text.find("CREATE TABLE `%s`" % table)
    if start < 0:
        raise AssertionError("no CREATE TABLE for %s in %s" % (table, path))
    ends = [end for end in (text.find("PRIMARY KEY", start), text.find("ROW_FORMAT", start))
            if end > start]
    end = min(ends) if ends else len(text)
    return re.findall(r"^\s*`([A-Za-z_0-9]+)`", text[start:end], re.M)


def _dump_tuples(values):
    """Yield each tuple's fields of a VALUES list, strings left quoted."""
    i, size = 0, len(values)
    while i < size:
        while i < size and values[i] != "(":
            i += 1
        if i >= size:
            return
        i += 1
        fields, current = [], []
        quote = None
        escaped = False
        while i < size:
            char = values[i]
            if quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    if i + 1 < size and values[i + 1] == quote:
                        i += 2
                        continue
                    quote = None
                i += 1
                continue
            if char in "'\"":
                quote = char
            elif char == ",":
                fields.append("".join(current).strip())
                current = []
            elif char == ")":
                fields.append("".join(current).strip())
                i += 1
                break
            else:
                current.append(char)
            i += 1
        yield fields


def _dump_value(text):
    text = text.strip()
    if text == "NULL" or not text:
        return None
    if text.startswith("'"):
        return text[1:-1]
    try:
        return int(text)
    except ValueError:
        return text


def _read_dump_table(path, table, wanted):
    """The rows of one table in a base dump, as dicts of the wanted columns."""
    text = path.read_text(encoding="utf-8", errors="replace")
    create_columns = _create_columns(text, table, path)
    pattern = re.compile(
        r"(?:INSERT|REPLACE)\s+INTO\s+`%s`\s*(?:\(([^)]*)\))?\s*VALUES\s*(.*)"
        % re.escape(table), re.IGNORECASE | re.S)
    rows = []
    for statement in _sql_statements(text):
        match = pattern.match(statement)
        if not match:
            continue
        columns = ([column.strip().strip("`") for column in match.group(1).split(",")]
                   if match.group(1) else create_columns)
        indexes = {name: index for index, name in enumerate(columns) if name in wanted}
        for fields in _dump_tuples(match.group(2)):
            row = {}
            for name, index in indexes.items():
                if index < len(fields):
                    row[name] = _dump_value(fields[index])
            if row:
                rows.append(row)
    return rows


def _read_spells(core=CORE_DIR):
    """spell_template keyed by entry, with the spell_mod overrides a/b/c read."""
    table = core / "sql" / "base" / "tw_world_spell_template.sql"
    spells = {}
    for row in _read_dump_table(table, "spell_template", SPELL_SOURCE_COLUMNS):
        entry = row.get("entry")
        if entry is not None:
            spells[entry] = {name: value for name, value in row.items() if name != "entry"}
    mod = core / "sql" / "base" / "tw_world_spell_mod.sql"
    if mod.is_file():
        for row in _read_dump_table(mod, "spell_mod", SPELL_MOD_COLUMNS):
            spell = spells.get(row.get("Id"))
            if spell is None:
                continue
            for column, field in (("Attributes", "attributes"),
                                  ("CastingTimeIndex", "castingTimeIndex"),
                                  ("InterruptFlags", "interruptFlags")):
                value = row.get(column, -1)
                if value != -1:
                    spell[field] = value
    return spells


def _shape_spell_ids(path):
    return [int(match.group(1)) for match in
            (re.match(r"^(\d+),", line.strip()) for line in path.read_text().splitlines())
            if match]


def _applies_aura(spell):
    return any(
        spell.get("effect%d" % index) in AURA_EFFECTS
        and spell.get("effectApplyAuraName%d" % index)
        for index in (1, 2, 3)
    )


def source_spell_problems(rows, spells, shape_ids):
    """Every disagreement between the CSV rows and the core's spell data."""
    problems = []

    def add(check, spell, where, detail):
        problems.append({"check": check, "spell": spell, "where": where, "detail": detail})

    for row in rows:
        where = "line %s (%s, boss %s)" % (row["_line"], row["map"], row["boss"])
        if row["row"] == "phase" and row["a"].startswith("aura="):
            spell_id = int(row["a"][5:])
            spell = spells.get(spell_id)
            if spell is None:
                add("phase-aura-missing", spell_id, where, "no spell_template row")
            elif spell["attributes"] & 0x40:
                add("phase-aura-passive", spell_id, where,
                    "attributes 0x%x has 0x40 (passive), so the aura never reaches the panel"
                    % spell["attributes"])
            continue
        if row["row"] != "ability":
            continue
        spell_id = int(row["key"])
        flags = boss_data.parse_flags(row["d"])
        spell = spells.get(spell_id)
        if spell is None:
            add("ability-missing", spell_id, where, "no spell_template row")
            continue
        kickable = spell["preventionType"] == 1 and spell["interruptFlags"] & 0x02
        nokick = flags["okNeeds"].get("interrupt") == "nokick"
        if flags["int"] and not nokick and not kickable:
            add("int-not-kickable", spell_id, where,
                "preventionType %s, interruptFlags 0x%x; the core's kick cannot stop it"
                % (spell["preventionType"], spell["interruptFlags"]))
        if nokick and kickable:
            add("nokick-kickable", spell_id, where,
                "preventionType %s, interruptFlags 0x%x; the core's kick can stop it"
                % (spell["preventionType"], spell["interruptFlags"]))
        if flags["self"] and not _applies_aura(spell):
            add("self-no-aura", spell_id, where,
                "effects %s/%s/%s apply no aura"
                % (spell.get("effect1", 0), spell.get("effect2", 0), spell.get("effect3", 0)))
    for spell_id in shape_ids:
        spell = spells.get(spell_id)
        where = "runtime/SpellShapeData.h"
        if spell is None:
            add("shape-missing", spell_id, where, "no spell_template row")
        elif spell["castingTimeIndex"] <= 1:
            add("shape-instant", spell_id, where,
                "castingTimeIndex %s, the spell is instant" % spell["castingTimeIndex"])
    return sorted(problems, key=lambda p: (p["check"], p["spell"], p["where"]))


@unittest.skipUnless(
    CORE_SPELL_SQL.is_file(),
    "set TORTOISE_CORE_DIR=<tortoise-wow checkout>: no %s" % CORE_SPELL_SQL,
)
class BossSpellSourceTest(unittest.TestCase):
    """Every raid's rows against the core's spell_template (skipped without a core)."""

    @classmethod
    def setUpClass(cls):
        cls.spells = _read_spells()
        cls.rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        cls.shapes = _shape_spell_ids(ROOT / "runtime" / "SpellShapeData.h")

    def source_problems(self):
        return source_spell_problems(self.rows, self.spells, self.shapes)

    def test_the_core_source_backs_every_row(self):
        unexpected = [
            problem for problem in self.source_problems()
            if (problem["check"], problem["spell"]) not in EXPECTED_SOURCE_PROBLEMS
        ]
        self.assertEqual(
            [], unexpected,
            "the core data disagrees with these rows; fix the row, or add a reason to "
            "EXPECTED_SOURCE_PROBLEMS when the row is right and the check is not:\n"
            + "\n".join("%s %s at %s: %s" % (p["check"], p["spell"], p["where"], p["detail"])
                        for p in unexpected),
        )

    def test_the_allow_list_still_reproduces(self):
        got = {(problem["check"], problem["spell"]) for problem in self.source_problems()}
        stale = sorted(set(EXPECTED_SOURCE_PROBLEMS) - got)
        self.assertEqual(
            [], stale,
            "these EXPECTED_SOURCE_PROBLEMS entries no longer reproduce; drop them and their "
            "comment: %s" % ", ".join("%s %s" % key for key in stale),
        )

    def test_the_check_catches_a_broken_aq40_row(self):
        spells = {sid: dict(values) for sid, values in self.spells.items()}
        rows = [dict(row) for row in self.rows]
        shapes = list(self.shapes)

        def broken_keys():
            return {(problem["check"], problem["spell"])
                    for problem in source_spell_problems(rows, spells, shapes)}

        # a: Ouro's enrage aura turned passive would never reach the panel.
        attributes = spells[26615]["attributes"]
        spells[26615]["attributes"] = attributes | 0x40
        self.assertIn(("phase-aura-passive", 26615), broken_keys())
        spells[26615]["attributes"] = attributes
        self.assertNotIn(("phase-aura-passive", 26615), broken_keys())

        # b: Sand Blast made instant while keeping its shape row.
        cast_index = spells[26102]["castingTimeIndex"]
        spells[26102]["castingTimeIndex"] = 1
        self.assertIn(("shape-instant", 26102), broken_keys())
        spells[26102]["castingTimeIndex"] = cast_index
        self.assertNotIn(("shape-instant", 26102), broken_keys())

        # c: Sweep says `int` with no nokick while the core cannot kick it.
        sweep = next(row for row in rows if row["map"] == "aq40" and row["key"] == "26103")
        sweep_flags = sweep["d"]
        sweep["d"] = "t=aoe;int"
        self.assertIn(("int-not-kickable", 26103), broken_keys())
        sweep["d"] = sweep_flags
        self.assertNotIn(("int-not-kickable", 26103), broken_keys())

        # d: Boulder claiming `self` although it only deals damage.
        boulder = next(row for row in rows if row["map"] == "aq40" and row["key"] == "26616")
        boulder_flags = boulder["d"]
        boulder["d"] = boulder_flags + ";self"
        self.assertIn(("self-no-aura", 26616), broken_keys())
        boulder["d"] = boulder_flags
        self.assertNotIn(("self-no-aura", 26616), broken_keys())


if __name__ == "__main__":
    unittest.main(verbosity=2)
