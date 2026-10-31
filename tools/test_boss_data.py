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
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import boss_data  # noqa: E402

# The committed table; BOSS_DATA_CSV points the value tests at another revision's table.
BOSS_CSV = Path(os.environ.get("BOSS_DATA_CSV", ROOT / "data" / "boss" / "boss_abilities.csv"))

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
        self.assertEqual(len(doc["bosses"]), 26)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 187)

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


# Blackwing Lair values, read from the core scripts and spell rows (below each boss), not generated from the
# table. Key: (boss creature entry, spell id). Value: (first, repeat_min, repeat_max, target, flags, phase,
# summon, hp, cast_ms); flags are the truthy ones sorted. A changed CSV field fails the test at the assertion.
BWL_ABILITY_VALUES = {
    # boss_razorgore.cpp: 19632 line 36/110/320-325; 24375 37/111/329-337; 22425 38/113/339-347;
    # 23023 39/112/349-357; 24776 40/114/309-318; 19832 44/213-226.
    (12435, 19632): (9000, 5000, 10000, "victim", "", "", 0, 0, 0),
    (12435, 24375): (22000, 25000, 45000, "aoe", "aura", "", 0, 0, 0),
    (12435, 22425): (7000, 15000, 20000, "aoe", "", "", 0, 0, 0),
    (12435, 23023): (12000, 15000, 25000, "aoe", "aura,cone", "", 0, 0, 0),
    (12435, 24776): (10000, 10000, 10000, "victim", "", "", 0, 0, 0),
    (12435, 19832): (0, 0, 0, "self", "self", "", 0, 0, 0),
    # boss_vaelastrasz.cpp: 19983 69/132/396-403; 23461 51/133/405-412; 23462 55/136/414-421;
    # 15847 59/137/423-430; 23513 47/188-203; 23620 63/134-135/353-387.
    (13020, 19983): (6000, 5000, 10000, "victim", "", "", 0, 0, 0),
    (13020, 23461): (8000, 5000, 10000, "victim", "aura,cone", "", 0, 0, 0),
    (13020, 23462): (4000, 2000, 2000, "aoe", "", "", 0, 0, 0),
    (13020, 15847): (8000, 4000, 6000, "aoe", "tail", "", 0, 0, 0),
    (13020, 23513): (0, 0, 0, "aoe", "", "", 0, 0, 0),
    (13020, 23620): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    # boss_broodlord_lashlayer.cpp: 15284 32/58/159-166; 23331 33/59/168-175; 24573 34/60/177-184;
    # 18670 35/61/186-193 (its first timer is urand(20000,25000), so no first value).
    (12017, 15284): (8000, 13000, 20000, "victim", "", "", 0, 0, 0),
    (12017, 23331): (20000, 20000, 35000, "aoe", "aura", "", 0, 0, 0),
    (12017, 24573): (25000, 20000, 30000, "victim", "aura", "", 0, 0, 0),
    (12017, 18670): (0, 12000, 25000, "victim", "", "", 0, 0, 0),
    # boss_firemaw.cpp: 22539 29/51/92-99; 23339 30/52/101-108; 23341 31/53/110-117; 3391 32/119-123.
    (11983, 22539): (16000, 16000, 16000, "aoe", "cone", "", 0, 0, 0),
    (11983, 23339): (30000, 30000, 30000, "victim", "cone", "", 0, 0, 0),
    (11983, 23341): (2000, 1800, 3000, "aoe", "aura", "", 0, 0, 0),
    (11983, 3391): (0, 0, 0, "self", "", "", 0, 0, 0),
    # boss_ebonroc.cpp: 22539 29/51/92-96; 23339 30/52/101-105; 23340 31/53/110-114; 3391 32/119-120.
    (14601, 22539): (16000, 16000, 16000, "aoe", "cone", "", 0, 0, 0),
    (14601, 23339): (30000, 30000, 30000, "victim", "cone", "", 0, 0, 0),
    (14601, 23340): (8000, 8000, 8000, "victim", "aura", "", 0, 0, 0),
    (14601, 3391): (0, 0, 0, "self", "", "", 0, 0, 0),
    # boss_flamegor.cpp: 22539 31/53/94-98; 23339 32/54/103-107; 23342 33/55/112-118; 3391 34/124-125;
    # 23462 is Frenzy's aura trigger (tw_world_spell_template.sql:18126, amplitude 2000).
    (11981, 22539): (16000, 16000, 16000, "aoe", "cone", "", 0, 0, 0),
    (11981, 23339): (30000, 30000, 30000, "victim", "cone", "", 0, 0, 0),
    (11981, 23342): (10000, 10000, 10000, "self", "self", "", 0, 0, 0),
    (11981, 23462): (0, 2000, 2000, "aoe", "", "", 0, 0, 0),
    (11981, 3391): (0, 0, 0, "self", "", "", 0, 0, 0),
    # boss_chromaggus.cpp: 22277-22281 35-39/115/246-283; 23308 43/116/285-301; 23310/23313/23315/23187
    # 44-47/117/285-301; 23153-23169 51-55/118/303-370; 23174 57/342-360 (AddAura when all five afflictions);
    # 23128 59/119/407-417; 28747 60/419-424.
    (14020, 22277): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (14020, 22278): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (14020, 22279): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (14020, 22280): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (14020, 22281): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (14020, 23308): (0, 0, 0, "aoe", "", "", 0, 0, 0),
    (14020, 23310): (0, 0, 0, "aoe", "aura", "", 0, 0, 0),
    (14020, 23313): (0, 0, 0, "aoe", "aura", "", 0, 0, 0),
    (14020, 23315): (0, 0, 0, "aoe", "aura", "", 0, 0, 0),
    (14020, 23187): (0, 0, 0, "aoe", "aura", "", 0, 0, 0),
    (14020, 23153): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23154): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23155): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23170): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23169): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23174): (0, 0, 0, "random", "aura", "", 0, 0, 0),
    (14020, 23128): (15000, 15000, 15000, "self", "self", "", 0, 0, 0),
    (14020, 28747): (0, 0, 0, "self", "self", "", 0, 20, 0),
    # boss_victor_nefarius.cpp: 22663 58/320-352; 22677 59/170/461-468; 22665 60/171/470-477;
    # 22678 61/172/479-489; 22666 62/173/491-501; 22667 63/174/548-560;
    # 22664 64/175/504-523 (first blink is the 1000 ms smoke).
    (10162, 22663): (0, 0, 0, "self", "self", "", 0, 0, 0),
    (10162, 22677): (5000, 3000, 10000, "victim", "int", "", 0, 0, 0),
    (10162, 22665): (15000, 15000, 15000, "aoe", "", "", 0, 0, 0),
    (10162, 22678): (8000, 15000, 25000, "random", "aura,int", "", 0, 0, 0),
    (10162, 22666): (20000, 25000, 40000, "random", "aura", "", 0, 0, 0),
    (10162, 22667): (25000, 25000, 40000, "random", "aura", "", 0, 0, 0),
    (10162, 22664): (1000, 20000, 25000, "random", "", "", 0, 0, 0),
    # boss_nefarian.cpp: 22539 40/80/302-309; 22686 41/81/311-318; 22687 42/82/320-327; 20691 43/83/329-336;
    # 23364 44/84/338-345; 22992 39/258-267; 17131 55/281-289; the nine class calls 45-53/165-240 (every
    # living player of the class, self-cast); 23603 54/367-408 (the random polymorph target); totem summons 665-679.
    (11583, 22539): (0, 18000, 25000, "aoe", "cone", "grounded", 0, 0, 0),
    (11583, 22686): (0, 25000, 30000, "aoe", "aura", "grounded", 0, 0, 0),
    (11583, 22687): (15000, 10000, 15000, "victim", "aura", "grounded", 0, 0, 0),
    (11583, 20691): (0, 7000, 10000, "victim", "", "grounded", 0, 0, 0),
    (11583, 23364): (10000, 4000, 8000, "aoe", "tail", "grounded", 0, 0, 0),
    (11583, 22992): (0, 0, 0, "self", "", "air", 0, 0, 0),
    (11583, 17131): (0, 0, 0, "self", "self", "air", 0, 0, 0),
    (11583, 23410): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23397): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23398): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23401): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23418): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23425): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23427): (0, 0, 0, "self", "aura", "grounded", 14668, 0, 0),
    (11583, 23436): (0, 0, 0, "self", "", "grounded", 0, 0, 0),
    (11583, 23414): (0, 0, 0, "self", "aura", "grounded", 0, 0, 0),
    (11583, 23603): (0, 0, 0, "random", "aura", "grounded", 0, 0, 0),
    (11583, 23419): (0, 0, 0, "self", "", "grounded", 14662, 0, 0),
    (11583, 23420): (0, 0, 0, "self", "", "grounded", 14663, 0, 0),
    (11583, 23423): (0, 0, 0, "self", "", "grounded", 14666, 0, 0),
    (11583, 23422): (0, 0, 0, "self", "", "grounded", 14664, 0, 0),
}

# Phase rules in rule order: (name, order, rule). Nefarian's ground phase is hp<20 now, so the start rule moved
# to order 3; Chromaggus' enrage is an aura rule, not a health rule.
BWL_PHASE_VALUES = {
    12435: [("eggs", 1, {"kind": "start"})],
    13020: [("fight", 1, {"kind": "start"})],
    12017: [("fight", 1, {"kind": "start"})],
    11983: [("fight", 1, {"kind": "start"})],
    14601: [("fight", 1, {"kind": "start"})],
    11981: [("fight", 1, {"kind": "start"})],
    14020: [("enraged", 1, {"kind": "aura", "aura": 28747}), ("fight", 2, {"kind": "start"})],
    10162: [("barrier", 1, {"kind": "aura", "aura": 22663}), ("fight", 2, {"kind": "start"})],
    11583: [
        ("air", 1, {"kind": "aura", "aura": 17131}),
        ("bones", 2, {"kind": "hp", "hp": 20}),
        ("grounded", 3, {"kind": "start"}),
    ],
}

FLAG_FIELDS = ("aura", "self", "mark", "ground", "cone", "tail", "int")
CLASS_CALLS = {23410, 23397, 23398, 23401, 23418, 23425, 23427, 23436, 23414}


class BwlValueTest(unittest.TestCase):
    """Every BWL row's values, not just its count or its presence in the JSON."""

    @classmethod
    def setUpClass(cls):
        rows = boss_data.read_rows(BOSS_CSV)
        names = boss_data.read_names(ROOT / "data" / "boss" / "names.json")
        raids = boss_data.read_raids(ROOT / "data" / "boss" / "raids.csv")
        _, bosses = boss_data.make_bosses(rows, names, raids)
        cls.bwl = {entry: boss for entry, boss in bosses.items() if boss["map"] == "bwl"}

    def test_bwl_ability_values(self):
        pinned = set()
        for entry, boss in self.bwl.items():
            for ability in boss["abilities"]:
                key = (entry, ability["sid"])
                self.assertIn(key, BWL_ABILITY_VALUES, boss["name"])
                pinned.add(key)
                flags = ",".join(f for f in FLAG_FIELDS if ability[f])
                self.assertEqual(
                    (
                        ability["a"],
                        ability["b"],
                        ability["c"],
                        ability["target"],
                        flags,
                        ability["phase"],
                        ability["summon"],
                        ability["hp"],
                        ability["castMs"],
                    ),
                    BWL_ABILITY_VALUES[key],
                    f"{boss['name']} {ability['sid']}",
                )
        self.assertEqual(pinned, set(BWL_ABILITY_VALUES))
        self.assertEqual(
            sum(len(b["abilities"]) for b in self.bwl.values()), len(BWL_ABILITY_VALUES)
        )

    def test_bwl_phase_values(self):
        self.assertEqual(sorted(self.bwl), sorted(BWL_PHASE_VALUES))
        for entry, expected in BWL_PHASE_VALUES.items():
            got = [(p["name"], p["order"], p["rule"]) for p in self.bwl[entry]["phases"]]
            self.assertEqual(got, expected, self.bwl[entry]["name"])

    def test_bwl_reviewed_rows(self):
        flamegor = {a["sid"]: a for a in self.bwl[11981]["abilities"]}
        self.assertIn(23462, flamegor)
        self.assertEqual((flamegor[23462]["b"], flamegor[23462]["c"]), (2000, 2000))
        chromaggus = {a["sid"]: a for a in self.bwl[14020]["abilities"]}
        self.assertIn(23174, chromaggus)
        self.assertEqual(chromaggus[23174]["target"], "random")

    def test_bwl_class_calls_are_self_targets(self):
        abilities = {a["sid"]: a for a in self.bwl[11583]["abilities"]}
        self.assertTrue(CLASS_CALLS <= set(abilities))
        for sid in CLASS_CALLS:
            self.assertNotEqual(abilities[sid]["target"], "random", str(sid))
            self.assertEqual(abilities[sid]["target"], "self", str(sid))
        self.assertEqual(abilities[23603]["target"], "random")


if __name__ == "__main__":
    unittest.main(verbosity=2)
