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
import os
import re
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

# Written from the AQ20 scripts (src/scripts/dungeons/ruins_of_ahnqiraj), not from the CSV: first ms after the pull
# (0 when the script rolls urand for it or the timer only runs inside a later phase, with the window in the note),
# repeat window ms, target, phase, flags, one-shot hp, whole-row ok reason, per-need ok reasons. castMs is pinned to 0
# (unknown) for every new spell.
AQ20_ABILITIES = {
    15348: [  # boss_kurinnaxx.cpp
        (25646, 0, 8000, 10000, "victim", "", ("aura",), 0, "", {"dispel": "nodispel"}),
        (25814, 0, 12000, 15000, "victim", "", ("cone",), 0, "", {}),
        (3391, 0, 12000, 17000, "victim", "", (), 0, "tank", {}),
        (8269, 0, 0, 0, "self", "", ("self",), 30, "", {"purge": "nopurge"}),
    ],
    15340: [  # boss_moam.cpp
        (15550, 6000, 15000, 15000, "aoe", "fight", (), 0, "heal", {}),
        (25672, 0, 0, 0, "aoe", "", (), 0, "heal", {}),
        (25685, 90000, 0, 0, "self", "", ("self",), 0, "phase", {}),
        (25676, 5000, 7000, 7000, "aoe", "fight", (), 0, "", {}),
    ],
    15370: [  # boss_buru.cpp
        (96, 1000, 6000, 6000, "victim", "phase1", ("aura",), 0, "", {"dispel": "nodispel"}),
        (1834, 9000, 9000, 9000, "self", "phase1", ("self",), 0, "", {"purge": "nopurge"}),
        (25640, 0, 0, 0, "self", "phase1", ("self",), 0, "", {"purge": "nopurge"}),
        (7155, 0, 0, 0, "self", "phase1", (), 0, "script", {}),
        (24721, 0, 0, 0, "self", "phase2", ("self",), 20, "phase", {}),
        (20512, 0, 6000, 6000, "aoe", "phase2", ("aura",), 0, "", {"dispel": "nodispel"}),
    ],
    15369: [  # boss_ayamiss.cpp
        (25748, 5000, 2000, 3000, "victim", "p1", ("aura", "int"), 0, "",
         {"interrupt": "nokick", "dispel": "nodispel"}),
        (25749, 10000, 6000, 20000, "aoe", "", (), 0, "heal", {}),
        (25725, 10000, 15000, 15000, "random", "", ("aura",), 0, "", {"dispel": "nodispel"}),
        (25852, 0, 8000, 15000, "victim", "p2", ("aura",), 0, "", {"dispel": "nodispel"}),
        (3391, 0, 5000, 7000, "victim", "p2", (), 0, "tank", {}),
        (8269, 0, 0, 0, "self", "", ("self",), 20, "", {"purge": "nopurge"}),
    ],
    15339: [  # boss_ossirian.cpp
        (25195, 30000, 20000, 30000, "aoe", "", ("aura",), 0, "", {}),
        (25176, 0, 45000, 45000, "self", "", ("self",), 0, "phase", {}),
        (25188, 25000, 30000, 30000, "aoe", "", (), 0, "heal", {}),
        (25189, 20000, 20000, 20000, "victim", "", ("aura",), 0, "", {"dispel": "nodispel"}),
    ],
}

AQ20_PHASES = {
    15348: [],
    15340: [("stone", 1, "aura=25685"), ("fight", 2, "start")],
    15370: [("phase2", 1, "hp<20"), ("phase1", 2, "start")],
    15369: [("p2", 1, "hp<70"), ("p1", 2, "start")],
    15339: [("strength", 1, "aura=25176"), ("vulnerable", 2, "start")],
}

AQ20_ADDS = {
    15340: [(15527, "summon", 1)],
    15370: [(15514, "summon", 0), (15521, "summon", 1)],
    15369: [(15546, "summon", 1), (15555, "summon", 1), (15934, "summon", 0)],
    15339: [(15428, "summon", 0)],
}


def rule_text(rule):
    if rule["kind"] == "start":
        return "start"
    if rule["kind"] == "hp":
        return "hp<%d" % rule["hp"]
    if rule["kind"] == "aura":
        return "aura=%d" % rule["aura"]
    return "%s=%s" % (rule["kind"], rule["flag"])


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

    def test_tank_needs_victim_or_random_target(self):
        # Patchwerk's Hateful Strike picks the highest-HP of the top four in melee: t=random is the
        # closest flag, and the note has to name that set. A self or aoe row cannot claim tank.
        rows = [ability(d="t=self;ok=tank"), add(),
                phase("p1", "start", 1, source="boss.cpp:3")]
        fx = self.make(rows)
        errors, _ = fx.validate()
        self.assertTrue(any("needs a t=victim or t=random row" in e for e in errors), errors)

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

    def test_aq20_values_match_the_source(self):
        # The fixture above is read off the scripts: a wrong timer, target, flag, phase, hp or cast time fails here even
        # when the CSV, the JSON and the citation gate agree with each other.
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        for entry, expected in AQ20_ABILITIES.items():
            boss = doc["bosses"][str(entry)]
            self.assertEqual(boss["map"], "aq20")
            got = []
            for a in boss["abilities"]:
                self.assertEqual(a["castMs"], 0, a["name"])
                flags = tuple(
                    k for k in ("aura", "self", "mark", "ground", "cone", "tail", "int") if a[k]
                )
                got.append(
                    (a["sid"], a["a"], a["b"], a["c"], a["target"], a["phase"], flags,
                     a["hp"], a["ok"], a["okNeeds"])
                )
            self.assertEqual(got, expected, entry)
            phases = [(p["name"], p["order"], rule_text(p["rule"])) for p in boss["phases"]]
            self.assertEqual(phases, AQ20_PHASES[entry], entry)
            self.assertEqual(
                [(x["entry"], x["role"], x["priority"]) for x in boss["adds"]],
                AQ20_ADDS.get(entry, []),
                entry,
            )

    def test_committed_data_builds_to_committed_json(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        committed = boss_data.DEFAULT_JSON.read_text(encoding="utf-8")
        self.assertEqual(committed, boss_data.dump_json(doc))
        self.assertEqual(len(doc["bosses"]), 121)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 612)

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

    # The Lower Karazhan expected values are read off the core scripts and
    # spell_template, not generated from the CSV (src/scripts/dungeons/
    # lower_karazhan_halls/boss_*.cpp; tw_world spell_template rows):
    # (a, b, c, target, aura, self, ground, cone, tail, int, summon, hp,
    #  phase, ok, okNeeds, castMs)
    KARA_LOWER_ABILITIES = {
        61221: {
            57063: (0, 9000, 12000, "aoe", True, False, False, False, False, False,
                    0, 0, "", "", {}, 0),
            57056: (11000, 12000, 15000, "random", True, False, False, False, False,
                    False, 0, 0, "", "", {}, 0),
        },
        61222: {
            57066: (0, 6000, 12000, "aoe", False, False, False, False, False, False,
                    0, 0, "", "heal", {}, 0),
            57073: (20000, 22000, 22000, "random", True, False, False, False, False,
                    False, 0, 0, "", "", {}, 0),
            57074: (0, 0, 0, "self", False, True, False, False, False, False,
                    0, 0, "", "", {"purge": "nopurge"}, 0),
            57075: (0, 26000, 42000, "random", True, False, False, False, False,
                    False, 0, 0, "", "", {"dispel": "nodispel"}, 0),
        },
        61224: {
            57094: (2000, 4000, 6000, "victim", True, False, False, False, False,
                    False, 0, 0, "", "tank", {}, 0),
            57093: (15000, 11000, 14000, "random", False, False, True, False, False,
                    True, 0, 0, "", "", {}, 0),
            57091: (30000, 25000, 34000, "self", False, True, False, False, False,
                    False, 0, 0, "", "", {"purge": "nopurge"}, 0),
        },
        61223: {
            57083: (2000, 1000, 1000, "aoe", True, False, False, False, False, False,
                    0, 0, "", "", {"dispel": "nodispel"}, 0),
            57076: (0, 8000, 11000, "random", True, False, False, False, False,
                    False, 0, 0, "", "", {"dispel": "nodispel"}, 0),
            57084: (50000, 48000, 48000, "aoe", True, False, False, False, False,
                    False, 0, 0, "", "", {}, 0),
            57082: (0, 0, 0, "self", False, True, False, False, False, False,
                    0, 30, "", "", {}, 0),
        },
        61225: {
            57095: (0, 20000, 26000, "victim", True, False, False, False, False,
                    False, 0, 0, "", "", {"dispel": "nodispel"}, 0),
            57096: (0, 15000, 18000, "aoe", True, False, False, False, False,
                    False, 0, 0, "", "", {}, 0),
            57097: (0, 0, 0, "random", True, False, False, False, False, False,
                    0, 0, "", "", {"dispel": "nodispel"}, 0),
            57098: (0, 0, 0, "random", True, False, False, False, False, False,
                    0, 0, "", "", {"dispel": "nodispel"}, 0),
            57099: (0, 8000, 15000, "random", False, False, False, False, False,
                    False, 0, 0, "below", "heal", {}, 0),
            57100: (0, 43000, 49000, "aoe", True, False, False, False, False,
                    False, 0, 0, "below", "", {}, 0),
            27564: (0, 33000, 45000, "self", False, True, False, False, False,
                    False, 0, 0, "below", "", {}, 0),
        },
    }
    KARA_LOWER_PHASES = {
        61221: [],
        61222: [],
        61224: [],
        61223: [
            ("frenzy", 1, {"kind": "aura", "aura": 57082}),
            ("fight", 2, {"kind": "start"}),
        ],
        61225: [
            ("below", 1, {"kind": "hp", "hp": 50}),
            ("fight", 2, {"kind": "start"}),
        ],
    }

    def kara_lower_doc(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        return boss_data.build_document(rows, names)

    def test_lower_karazhan_ability_values(self):
        doc = self.kara_lower_doc()
        for entry, expected in self.KARA_LOWER_ABILITIES.items():
            boss = doc["bosses"][str(entry)]
            actual = {
                ability["sid"]: (
                    ability["a"], ability["b"], ability["c"], ability["target"],
                    ability["aura"], ability["self"], ability["ground"],
                    ability["cone"], ability["tail"], ability["int"],
                    ability["summon"], ability["hp"], ability["phase"],
                    ability["ok"], ability["okNeeds"], ability["castMs"],
                )
                for ability in boss["abilities"]
            }
            self.assertEqual(actual, expected, boss["name"])

    def test_lower_karazhan_phase_rules(self):
        doc = self.kara_lower_doc()
        for entry, expected in self.KARA_LOWER_PHASES.items():
            actual = [
                (phase["name"], phase["order"], phase["rule"])
                for phase in doc["bosses"][str(entry)]["phases"]
            ]
            self.assertEqual(actual, expected, str(entry))

        # A random first timer and every shared or frozen timer stay out of the
        # columns; the rows whose first cast depends on a later event have a=0.
        boss = doc["bosses"]["61225"]
        by_sid = {a["sid"]: a for a in boss["abilities"]}
        self.assertEqual(by_sid[57097]["a"], 0)  # two re-arm windows
        self.assertEqual(by_sid[57098]["a"], 0)  # triggered by 57097
        self.assertEqual(by_sid[57099]["phase"], "below")
        self.assertEqual(by_sid[57097]["phase"], "")

        # Instant or unpreventable spells are not kickable, however the plan's
        # tactic table reads.
        self.assertFalse(by_sid[57099]["int"])
        grizikil = {a["sid"]: a for a in doc["bosses"]["61224"]["abilities"]}
        self.assertFalse(grizikil[57094]["int"])
        self.assertTrue(grizikil[57093]["int"])
        self.assertTrue(grizikil[57093]["ground"])

        # The summon add rows and their kill priorities.
        adds = {a["entry"]: (a["role"], a["priority"]) for a in doc["bosses"]["61221"]["adds"]}
        self.assertEqual(adds[30008], ("summon", 0))
        self.assertEqual(adds[61206], ("summon", 2))
        self.assertEqual(
            {a["entry"] for a in doc["bosses"]["61224"]["adds"]},
            {61195, 61196, 61197},
        )


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
    # 23364 44/84/338-345; 22992 39/258-267; 17131 55/281-289; the nine class calls 45-53/184-237 (class-wide;
    # only paladin, hunter and warlock are casts, the rest apply the aura with AddAura); 23603 54/367-408 (a random
    # target from Nefarian's threat list); totem summons 665-679.
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

# Phase rules in rule order: (name, order, rule). Nefarian's ground phase is the start rule: the Bone Construct
# spawn below 20 percent is a note, because the panel's phases are exclusive and an hp<20 row would hide his ground
# abilities; Chromaggus' enrage is an aura rule, not a health rule.
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
        ("grounded", 2, {"kind": "start"}),
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


# Naxxramas pins, written from the core scripts (boss_*.cpp and naxxramas.h in ~/wt-core-bossdata), not from the CSV.
# (spell, first, repeatMin, repeatMax, target, phase, extra flags, castMs)
NAXX_ABILITIES = {
    15956: [
        (28783, 0, 12000, 18000, "random", "", {"cone"}, 0),
        (28785, 0, 90000, 110000, "self", "", {"self", "summon=16573", "ok.purge=nopurge"}, 0),
    ],
    15953: [
        (28796, 8000, 10000, 12000, "aoe", "", {"aura"}, 0),
        (28794, 16000, 8000, 12000, "random", "", {"ground"}, 0),
        (28798, 60000, 60000, 60000, "self", "", {"self", "ok.purge=nopurge"}, 0),
    ],
    15952: [
        (28622, 20000, 40000, 40000, "random", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29484, 40000, 40000, 40000, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28741, 9000, 9000, 11000, "victim", "", {"cone"}, 0),
        (28776, 15000, 5000, 10000, "victim", "", {"aura"}, 0),
        (29434, 30000, 40000, 40000, "self", "", {"summon=17055"}, 0),
        (28747, 0, 0, 0, "self", "", {"self", "hp30", "ok.purge=nopurge"}, 0),
    ],
    15954: [
        (29213, 0, 0, 0, "victim", "", {"aura"}, 0),
        (29212, 0, 0, 0, "aoe", "", {"aura"}, 0),
        (29208, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29209, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29210, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29211, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29216, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29231, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29247, 0, 0, 0, "self", "", {"summon=16984"}, 0),
        (29217, 0, 0, 0, "self", "", {"summon=16983"}, 0),
        (29226, 0, 0, 0, "self", "", {"summon=16981"}, 0),
    ],
    15936: [
        (29998, 0, 0, 0, "aoe", "", {"aura"}, 0),
        (29350, 0, 0, 0, "aoe", "", {"aura"}, 0),
        (29371, 0, 0, 0, "aoe", "", set(), 0),
        (29310, 0, 0, 0, "aoe", "", set(), 0),
        (30211, 0, 0, 0, "self", "", {"ok=script"}, 0),
    ],
    16011: [
        (29201, 0, 0, 0, "aoe", "", set(), 0),
        (29194, 0, 0, 0, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29196, 0, 0, 0, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29198, 0, 0, 0, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29865, 0, 0, 0, "aoe", "", {"aura"}, 0),
        (29204, 0, 0, 0, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (30281, 0, 0, 0, "self", "", {"ok=script"}, 0),
        (29407, 0, 0, 0, "random", "", {"aura", "ok.dispel=nodispel"}, 0),
    ],
    16061: [
        (26613, 0, 0, 0, "victim", "", {"ok=tank"}, 0),
        (29107, 0, 0, 0, "aoe", "", set(), 0),
        (29125, 0, 0, 0, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29051, 0, 0, 0, "self", "", {"aura", "ok.dispel=nodispel"}, 0),
    ],
    16060: [
        (29317, 1000, 1600, 2000, "random", "", {"ok=heal"}, 0),
        (28679, 1000, 20000, 20000, "aoe", "", {"aura"}, 0),
        (28025, 15000, 15000, 20000, "self", "", {"ok=script"}, 0),
        (28026, 15000, 15000, 20000, "self", "", {"ok=script"}, 0),
    ],
    16065: [
        (28833, 20000, 12000, 12000, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28863, 0, 0, 0, "random", "", set(), 0),
        (29061, 0, 0, 0, "self", "", {"self", "hp50", "ok.purge=nopurge"}, 0),
        (28931, 0, 0, 0, "self", "", {"ok=script"}, 0),
    ],
    16062: [
        (28834, 20000, 12000, 12000, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28881, 0, 0, 0, "self", "", {"self", "ok.purge=nopurge"}, 0),
        (28882, 0, 0, 0, "random", "", {"aura", "ok.dispel=nodispel"}, 0),
        (29061, 0, 0, 0, "self", "", {"self", "hp50", "ok.purge=nopurge"}, 0),
        (28928, 0, 0, 0, "self", "", {"ok=script"}, 0),
    ],
    16064: [
        (28832, 20000, 12000, 12000, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28884, 0, 0, 0, "random", "", set(), 0),
        (29061, 0, 0, 0, "self", "", {"self", "hp50", "ok.purge=nopurge"}, 0),
        (28932, 0, 0, 0, "self", "", {"ok=script"}, 0),
    ],
    16063: [
        (28835, 20000, 12000, 12000, "aoe", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28883, 0, 0, 0, "random", "", {"ok=heal"}, 0),
        (29061, 0, 0, 0, "self", "", {"self", "hp50", "ok.purge=nopurge"}, 0),
        (28934, 0, 0, 0, "self", "", {"ok=script"}, 0),
    ],
    16028: [
        (28308, 1200, 1200, 1200, "random", "", {"ok=tank"}, 0),
        (28131, 0, 0, 0, "self", "", {"self", "hp5", "ok.purge=nopurge"}, 0),
        (27680, 0, 0, 0, "self", "", {"self", "ok.purge=nopurge"}, 0),
        (32309, 0, 5000, 5000, "aoe", "", {"aura"}, 0),
    ],
    15931: [
        (28137, 3000, 1500, 1500, "victim", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28169, 12000, 7000, 13000, "random", "", {"aura"}, 0),
        (28157, 20000, 30000, 35000, "victim", "", {"cone"}, 0),
        (28240, 16000, 16000, 16000, "self", "", {"ok=script"}, 0),
        (26662, 0, 0, 0, "self", "", {"self", "ok.purge=nopurge"}, 0),
    ],
    15932: [
        (25646, 10000, 10000, 10000, "victim", "", {"aura", "ok.dispel=nodispel"}, 0),
        (28374, 105000, 105000, 105000, "aoe", "", {"ok=heal"}, 0),
        (28375, 0, 0, 0, "aoe", "", {"ok=heal"}, 0),
        (28371, 10000, 10000, 10000, "self", "", {"self"}, 0),
        (29685, 20000, 20000, 20000, "aoe", "", set(), 0),
        (26662, 330000, 300000, 300000, "self", "", {"self", "ok.purge=nopurge"}, 0),
        (29307, 0, 0, 0, "self", "", {"self", "ok.purge=nopurge"}, 0),
    ],
    15928: [
        (28125, 15000, 15000, 20000, "aoe", "adds", {"ok=heal"}, 0),
        (28135, 6000, 6000, 6000, "aoe", "adds", {"ok=heal"}, 0),
        (28134, 25000, 25000, 30000, "self", "adds", {"self", "ok.purge=nopurge"}, 0),
        (28337, 20500, 20500, 20500, "victim", "adds", set(), 0),
        (28160, 0, 0, 0, "self", "", {"self", "ok=phase"}, 0),
        (28089, 0, 30000, 30000, "self", "fight", set(), 0),
        (28059, 0, 0, 0, "aoe", "fight", {"aura", "ok.dispel=nodispel"}, 0),
        (28084, 0, 0, 0, "aoe", "fight", {"aura", "ok.dispel=nodispel"}, 0),
        (28167, 5000, 5000, 7000, "random", "fight", {"ok=heal"}, 0),
        (28299, 1000, 1500, 1500, "victim", "fight", {"ok=heal"}, 0),
        (27680, 0, 0, 0, "self", "fight", {"self", "ok.purge=nopurge"}, 0),
    ],
    15989: [
        (28542, 0, 0, 0, "aoe", "ground", {"aura"}, 0),
        (28547, 0, 0, 0, "aoe", "", {"ground"}, 0),
        (19983, 0, 0, 0, "victim", "ground", {"ok=tank"}, 0),
        (15847, 0, 0, 0, "aoe", "ground", {"tail"}, 0),
        (26662, 900000, 300000, 300000, "self", "", {"self", "ok.purge=nopurge"}, 0),
        (28522, 0, 3500, 3500, "random", "air", set(), 0),
        (28524, 0, 0, 0, "aoe", "air", set(), 0),
        (30101, 0, 0, 0, "self", "air", {"ok=script"}, 0),
        (29327, 0, 0, 0, "aoe", "air", set(), 0),
        (29328, 0, 0, 0, "aoe", "air", set(), 0),
        (28529, 0, 0, 0, "self", "", {"self", "ok.purge=nopurge"}, 0),
    ],
    15990: [
        (29423, 0, 0, 0, "self", "adds", {"self", "ok=script"}, 0),
        (28478, 0, 0, 0, "victim", "fight", {"int"}, 0),
        (28479, 0, 0, 0, "aoe", "fight", set(), 0),
        (27810, 0, 0, 0, "random", "fight", set(), 0),
        (27812, 0, 0, 0, "aoe", "", set(), 0),
        (27819, 0, 0, 0, "random", "fight", {"aura", "ok.dispel=nodispel"}, 0),
        (27808, 0, 0, 0, "random", "fight", {"aura", "ok.dispel=nodispel"}, 0),
        (28408, 0, 0, 0, "self", "fight", set(), 0),
        (28498, 0, 300000, 300000, "self", "fight", {"self", "ok.purge=nopurge"}, 0),
        (28460, 0, 0, 0, "self", "adds", {"self", "ok.purge=nopurge"}, 0),
        (28458, 0, 0, 0, "self", "adds", {"self", "ok.purge=nopurge"}, 0),
        (25646, 7500, 7500, 7500, "victim", "adds", {"aura", "ok.dispel=nodispel"}, 0),
        (28457, 0, 0, 0, "aoe", "adds", set(), 0),
    ],
}

# (name, rule kind, rule value, order)
NAXX_PHASES = {
    15954: [("ground", "start", 0, 1)],
    16060: [("ground", "start", 0, 1)],
    15928: [("adds", "aura", 28160, 1), ("fight", "start", 0, 2)],
    15989: [("air", "flag", "h", 1), ("ground", "start", 0, 2)],
    15990: [("adds", "flag", "i", 1), ("fight", "start", 0, 2)],
}


def _ability_flag_tokens(ability):
    tokens = {name for name in ("aura", "self", "mark", "ground", "cone", "tail", "int") if ability[name]}
    if ability["hp"]:
        tokens.add(f"hp{ability['hp']}")
    if ability["summon"]:
        tokens.add(f"summon={ability['summon']}")
    if ability["ok"]:
        tokens.add(f"ok={ability['ok']}")
    for need, reason in ability["okNeeds"].items():
        tokens.add(f"ok.{need}={reason}")
    return tokens


class NaxxValueTest(unittest.TestCase):
    """Pins every Naxxramas ability and phase against the core scripts, not against the CSV."""

    def setUp(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        self.doc = boss_data.build_document(rows, names)

    def test_ability_values(self):
        for entry, expected in NAXX_ABILITIES.items():
            boss = self.doc["bosses"][str(entry)]
            actual = {a["sid"]: a for a in boss["abilities"]}
            self.assertEqual(set(actual), {row[0] for row in expected}, boss["name"])
            for sid, first, rep_min, rep_max, target, phase, flags, cast_ms in expected:
                ability = actual[sid]
                where = f"{boss['name']} {sid}"
                self.assertEqual(
                    (ability["a"], ability["b"], ability["c"]), (first, rep_min, rep_max), where
                )
                self.assertEqual(ability["target"], target, where)
                self.assertEqual(ability["phase"], phase, where)
                self.assertEqual(ability["castMs"], cast_ms, where)
                self.assertEqual(_ability_flag_tokens(ability), flags, where)

    def test_raid_wide_abilities_are_aoe(self):
        # Corrupted Mind is one boss cast whose spell script picks the class debuff; the charges are
        # one cast per living player on itself. Neither is a single random target.
        raid_wide = {16011: (29201, 29194, 29196, 29198), 15928: (28059, 28084)}
        for entry, sids in raid_wide.items():
            boss = self.doc["bosses"][str(entry)]
            abilities = {a["sid"]: a for a in boss["abilities"]}
            for sid in sids:
                self.assertEqual(abilities[sid]["target"], "aoe", f"{boss['name']} {sid}")

    def test_no_naxx_phase_uses_passive_aura(self):
        # 29230 Immune All is passive, so the module never sends it and such a phase can never fire.
        for boss in self.doc["bosses"].values():
            if boss["map"] != "naxxramas":
                continue
            for phase in boss["phases"]:
                self.assertNotEqual(
                    phase["rule"].get("aura"), 29230, f"{boss['name']} phase {phase['name']}"
                )

    def test_instant_naxx_spells_have_no_shape_row(self):
        # Poison Shock, Impale and Slime Spray are instant (castingTimeIndex 1), so a cone overlay never draws.
        shapes = (ROOT / "runtime" / "SpellShapeData.h").read_text(encoding="utf-8")
        ids = {
            int(line.split(",", 1)[0])
            for line in shapes.splitlines()
            if line[:1].isdigit()
        }
        self.assertFalse(ids & {28741, 28783, 28157})

    def test_mutating_injection_repeat_window(self):
        ability = {a["sid"]: a for a in self.doc["bosses"]["15931"]["abilities"]}[28169]
        self.assertEqual((ability["b"], ability["c"]), (7000, 13000))

    def test_phase_values(self):
        naxx = [b for b in self.doc["bosses"].values() if b["map"] == "naxxramas"]
        self.assertEqual(len(naxx), 18)
        for entry, expected in NAXX_PHASES.items():
            phases = self.doc["bosses"][str(entry)]["phases"]
            actual = []
            for phase in phases:
                rule = phase["rule"]
                if rule["kind"] == "start":
                    value = 0
                elif rule["kind"] == "aura":
                    value = rule["aura"]
                elif rule["kind"] == "hp":
                    value = rule["hp"]
                else:
                    value = rule["flag"]
                actual.append((phase["name"], rule["kind"], value, phase["order"]))
            self.assertEqual(actual, expected, entry)


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
    "entry", "attributes", "attributesEx", "preventionType", "interruptFlags",
    "channelInterruptFlags", "castingTimeIndex",
    "effect1", "effect2", "effect3",
    "effectApplyAuraName1", "effectApplyAuraName2", "effectApplyAuraName3",
}
SPELL_MOD_COLUMNS = {
    "Id", "Attributes", "AttributesEx", "CastingTimeIndex", "InterruptFlags",
    "ChannelInterruptFlags",
}

# SpellDefines.h: SPELL_PREVENTION_TYPE_SILENCE (130), SPELL_INTERRUPT_FLAG_DAMAGE
# (439), CHANNEL_FLAG_INTERRUPT (449), SPELL_ATTR_EX_CHANNELED_1/_2 (767, 771).
SPELL_PREVENTION_TYPE_SILENCE = 1
SPELL_INTERRUPT_FLAG_DAMAGE = 0x02
CHANNEL_FLAG_INTERRUPT = 0x04
SPELL_ATTR_EX_CHANNELED = 0x04 | 0x40

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
    # Zul'Gurub rows marked nokick although the core's channel branch can stop
    # them: both are channels (attributesEx 0x4004) with CHANNEL_FLAG_INTERRUPT
    # (0x04) set (0x7c2c, 0x100c), so a kick ends the channel. The row, not the
    # check, is wrong. Pre-existing ZG data, worth a separate fix.
    ("nokick-kickable", 23953): "Jeklik Mind Flay, an interruptible channel",
    ("nokick-kickable", 24300): "Mar'li Drain Life, an interruptible channel",
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
                                  ("AttributesEx", "attributesEx"),
                                  ("CastingTimeIndex", "castingTimeIndex"),
                                  ("InterruptFlags", "interruptFlags"),
                                  ("ChannelInterruptFlags", "channelInterruptFlags")):
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


def _kickable(spell):
    """Spell::EffectInterruptCast: the prevention type gates both slots, and a
    channel is stopped by its channel flags, not by InterruptFlags."""
    if spell["preventionType"] != SPELL_PREVENTION_TYPE_SILENCE:
        return False
    if spell["attributesEx"] & SPELL_ATTR_EX_CHANNELED:
        return bool(spell["channelInterruptFlags"] & CHANNEL_FLAG_INTERRUPT)
    return bool(spell["interruptFlags"] & SPELL_INTERRUPT_FLAG_DAMAGE)


def _interrupt_data(spell):
    if spell["attributesEx"] & SPELL_ATTR_EX_CHANNELED:
        return ("channeled (attributesEx 0x%x), preventionType %s, channelInterruptFlags 0x%x"
                % (spell["attributesEx"], spell["preventionType"],
                   spell["channelInterruptFlags"]))
    return ("generic, preventionType %s, interruptFlags 0x%x"
            % (spell["preventionType"], spell["interruptFlags"]))


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
        kickable = _kickable(spell)
        nokick = flags["okNeeds"].get("interrupt") == "nokick"
        if flags["int"] and not nokick and not kickable:
            add("int-not-kickable", spell_id, where,
                "%s; the core's kick cannot stop it" % _interrupt_data(spell))
        if nokick and kickable:
            add("nokick-kickable", spell_id, where,
                "%s; the core's kick can stop it" % _interrupt_data(spell))
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


def _interrupt_fixture(prevention=SPELL_PREVENTION_TYPE_SILENCE, interrupt=0,
                       channel=0, attributes_ex=0):
    return {
        "preventionType": prevention,
        "interruptFlags": interrupt,
        "channelInterruptFlags": channel,
        "attributesEx": attributes_ex,
    }


class InterruptRuleTest(unittest.TestCase):
    """Spell::EffectInterruptCast (SpellEffects.cpp:3408-3412) branch by slot."""

    def test_a_generic_cast_with_the_damage_flag_is_kickable(self):
        self.assertTrue(_kickable(_interrupt_fixture(interrupt=SPELL_INTERRUPT_FLAG_DAMAGE)))

    def test_a_generic_cast_without_the_damage_flag_is_not_kickable(self):
        self.assertFalse(_kickable(_interrupt_fixture(interrupt=0x0d)))

    def test_a_channel_is_kickable_by_its_channel_flags(self):
        spell = _interrupt_fixture(interrupt=0x0d, channel=CHANNEL_FLAG_INTERRUPT,
                                   attributes_ex=SPELL_ATTR_EX_CHANNELED)
        self.assertTrue(_kickable(spell))

    def test_a_channel_ignores_interrupt_flags(self):
        spell = _interrupt_fixture(interrupt=SPELL_INTERRUPT_FLAG_DAMAGE,
                                   attributes_ex=SPELL_ATTR_EX_CHANNELED)
        self.assertFalse(_kickable(spell))

    def test_the_prevention_type_gates_the_channel_branch_too(self):
        spell = _interrupt_fixture(prevention=2, channel=CHANNEL_FLAG_INTERRUPT,
                                   attributes_ex=SPELL_ATTR_EX_CHANNELED)
        self.assertFalse(_kickable(spell))


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

        # e: Great Heal `int` while the spell becomes a channel with no channel
        # interrupt flag: the core's channel branch cannot stop it.
        heal = spells[25807]
        heal_flags = dict(heal)
        heal["attributesEx"] = SPELL_ATTR_EX_CHANNELED
        heal["channelInterruptFlags"] = 0
        self.assertIn(("int-not-kickable", 25807), broken_keys())
        heal.clear()
        heal.update(heal_flags)
        self.assertNotIn(("int-not-kickable", 25807), broken_keys())


# Upper Karazhan Halls, read off the Turtle core sources (src/scripts/dungeons/upper_karazhan_halls and the
# tw_world spell/creature/spell-list rows), NOT generated from the CSV. Every field a row can set is pinned:
# timers, target, each flag, the ok reason, castMs, and the phase rules.
KARA_DEFAULTS = {
    "a": 0,
    "b": 0,
    "c": 0,
    "target": "",
    "phase": "",
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
    "castMs": 0,
}


def kara_ability(**fields):
    row = dict(KARA_DEFAULTS)
    row.update(fields)
    return row


# entry -> {spell id: expected ability fields}
KARA_ABILITIES = {
    61939: {  # Keeper Gnarlmoon, boss_keeper_gnarlmoon.cpp + creature_spells 619390
        51080: kara_ability(target="aoe", aura=True, okNeeds={"dispel": "nodispel"}),
        51081: kara_ability(target="aoe", aura=True, okNeeds={"dispel": "nodispel"}),
        51082: kara_ability(target="aoe", ok="heal"),
        51083: kara_ability(target="self", summon=59999),
        51084: kara_ability(target="random", aura=True),
        51085: kara_ability(target="aoe", ok="heal"),
        51086: kara_ability(target="aoe"),
        51096: kara_ability(target="self", self=True, ok="phase"),
    },
    61946: {  # Ley-Watcher Incantagos
        51176: kara_ability(target="self", self=True, ok="phase"),
        51178: kara_ability(b=45000, c=55000, target="self", summon=59989),
        51179: kara_ability(b=30000, c=35000, target="self", summon=59955),
        51185: kara_ability(b=30000, c=30000, target="self", summon=59987),
        51180: kara_ability(b=14000, c=17000, target="random", ground=True),
        51181: kara_ability(a=7000, b=30000, c=35000, target="random", aura=True, int=True),
        51182: kara_ability(target="victim", ground=True, int=True),
        51184: kara_ability(target="victim", int=True),
        51186: kara_ability(b=13000, c=13000, target="aoe", aura=True),
        26662: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    61951: {  # Anomalus
        51100: kara_ability(a=7500, target="random", aura=True, okNeeds={"dispel": "nodispel"}),
        51102: kara_ability(a=14000, target="random", summon=59996),
        51107: kara_ability(a=3000, target="random", aura=True),
    },
    61958: {  # Echo of Medivh
        51111: kara_ability(b=20000, c=25000, target="aoe", aura=True, okNeeds={"dispel": "nodispel"}),
        # Interrupting him is punished: these stay unflagged so the bots do not kick (any interrupt effect
        # adds a Guardian's Ire stack, 4 stacks cast Medivh's Fury).
        51112: kara_ability(target="random", aura=True, okNeeds={"dispel": "nodispel"}),
        51113: kara_ability(target="random", ground=True),
        51114: kara_ability(target="aoe", aura=True),
        51116: kara_ability(target="random", aura=True),
        51118: kara_ability(target="random", aura=True),
        51120: kara_ability(target="random"),
        51121: kara_ability(target="random"),
        51122: kara_ability(target="random", summon=59994),
        51125: kara_ability(target="self", self=True, ok="phase"),
        8269: kara_ability(target="self", self=True, hp=20, ok="phase"),
        51110: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51109: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59967: {  # King, the chess event
        28863: kara_ability(a=20000, b=20000, c=20000, target="random"),
        51230: kara_ability(b=8000, c=14000, target="self", summon=59972, int=True),
        51231: kara_ability(b=40000, c=50000, target="aoe", aura=True),
        51228: kara_ability(target="self", self=True, ok="phase"),
        51229: kara_ability(target="aoe", ok="heal"),
    },
    59971: {  # Bishop, its own block so the panel finds it by entry
        51212: kara_ability(target="victim"),
        51213: kara_ability(target="victim"),
        51214: kara_ability(a=12000, b=12000, c=12000, target="ally"),
        51215: kara_ability(a=15000, b=15000, c=15000, target="aoe", ok="heal"),
        51216: kara_ability(a=30000, b=30000, c=30000, target="ally"),
        51217: kara_ability(a=14000, b=14000, c=14000, target="random", aura=True),
        51218: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59970: {  # Rook
        51219: kara_ability(a=11000, b=11000, c=11000, target="victim", ok="tank"),
        51220: kara_ability(a=20000, b=20000, c=20000, target="random"),
        51221: kara_ability(a=35000, b=35000, c=35000, target="self", self=True,
                            okNeeds={"purge": "nopurge"}),
        51223: kara_ability(a=25000, b=25000, c=25000, target="self"),
        51218: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59968: {  # Knight
        51224: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51225: kara_ability(a=1300, b=1300, c=1300, target="victim", ok="tank"),
        51226: kara_ability(a=11000, b=11000, c=11000, target="victim", ok="tank"),
        51227: kara_ability(a=20000, b=20000, c=30000, target="self", summon=59969),
        51218: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59981: {  # Sanv Tas'dal
        51194: kara_ability(a=5000, b=13000, c=13000, target="random", aura=True),
        51195: kara_ability(a=3000, b=11000, c=11000, target="random", aura=True),
        51196: kara_ability(a=7000, b=15000, c=15000, target="aoe", aura=True),
        51198: kara_ability(a=1500, b=5500, c=5500, target="random", aura=True, int=True),
        51204: kara_ability(target="aoe", int=True),
        51205: kara_ability(b=45000, c=45000, target="aoe", ok="heal"),
        51209: kara_ability(target="self", hp=50, ok="phase"),
        51206: kara_ability(target="self", self=True, hp=20, ok="phase"),
        51207: kara_ability(target="self", self=True, hp=20, ok="phase"),
    },
    59991: {  # Kruul
        51164: kara_ability(a=2000, b=4000, c=4000, target="victim", ok="tank"),
        51166: kara_ability(a=30000, target="random", aura=True),
        51170: kara_ability(a=25000, target="random", summon=59990),
        51172: kara_ability(target="aoe", hp=25, ok="phase"),
        51173: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51174: kara_ability(b=10000, c=10000, target="aoe", ok="heal", int=True),
    },
    59961: {},  # Rupturan the Broken: no script, no spells in this core
}

# entry -> [(name, rule, order)]
KARA_PHASES = {
    61939: [("intermission", {"kind": "aura", "aura": 51096}, 1), ("fight", {"kind": "start"}, 2)],
    61946: [
        ("charged", {"kind": "aura", "aura": 51176}, 1),
        ("devouring", {"kind": "hp", "hp": 40}, 2),
        ("exposed", {"kind": "start"}, 3),
    ],
    61958: [
        ("enraged", {"kind": "aura", "aura": 8269}, 1),
        ("shadow", {"kind": "aura", "aura": 51125}, 2),
        ("fight", {"kind": "start"}, 3),
    ],
    59967: [("shielded", {"kind": "aura", "aura": 51228}, 1), ("fight", {"kind": "start"}, 2)],
    59981: [
        ("enraged", {"kind": "hp", "hp": 20}, 1),
        ("rift", {"kind": "hp", "hp": 50}, 2),
        ("portals", {"kind": "hp", "hp": 80}, 3),
        ("fight", {"kind": "start"}, 4),
    ],
    59991: [("enraged", {"kind": "aura", "aura": 51173}, 1), ("fight", {"kind": "start"}, 2)],
}


class KaraUpperValueTest(unittest.TestCase):
    """Every Upper Karazhan ability and phase rule against the core, field by field."""

    @classmethod
    def setUpClass(cls):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        cls.map_doc, cls.bosses = boss_data.make_bosses(rows, names)

    def test_every_ability_value(self):
        for entry, expected in KARA_ABILITIES.items():
            boss = self.bosses[entry]
            self.assertEqual(boss["map"], "upper_karazhan_halls")
            got = {ability["sid"]: ability for ability in boss["abilities"]}
            self.assertEqual(set(got), set(expected), f"boss {entry} spell set")
            for sid, fields in expected.items():
                for key, value in fields.items():
                    self.assertEqual(
                        got[sid][key], value, f"boss {entry} spell {sid} field {key}"
                    )

    def test_every_phase_rule(self):
        for entry in KARA_ABILITIES:
            got = [
                (phase["name"], phase["rule"], phase["order"])
                for phase in self.bosses[entry]["phases"]
            ]
            self.assertEqual(got, KARA_PHASES.get(entry, []), f"boss {entry} phases")

    def test_phases_label_but_do_not_hide_abilities(self):
        # Kara's phases do not stop the whole ability set for the rest of the fight (Sanv resumes at 50%,
        # Echo keeps the random list in every phase), so no ability carries p=<phase>.
        for entry in KARA_ABILITIES:
            for ability in self.bosses[entry]["abilities"]:
                self.assertEqual(ability["phase"], "", f"boss {entry} spell {ability['sid']}")

    def test_semantic_choices_from_the_core(self):
        def ability(entry, sid):
            return next(a for a in self.bosses[entry]["abilities"] if a["sid"] == sid)

        # Creature-spell-list timers are 0 (one-line dump); the two Shared random picks are 0 too.
        for sid in (51082, 51083, 51084, 51086):
            self.assertEqual(
                (ability(61939, sid)["a"], ability(61939, sid)["b"], ability(61939, sid)["c"]),
                (0, 0, 0),
                f"Gnarlmoon {sid}",
            )
        for sid in (51182, 51184):
            row = ability(61946, sid)
            self.assertEqual((row["a"], row["b"], row["c"]), (0, 0, 0), f"Incantagos {sid}")
        # Anomalus's repeats scale with health: no repeat in the columns.
        for sid in (51100, 51102, 51107):
            row = ability(61951, sid)
            self.assertEqual((row["b"], row["c"]), (0, 0), f"Anomalus {sid}")
        # Echo's random cast list has no timer at all.
        for sid in range(51112, 51123):
            if sid in (51115, 51117, 51119):
                continue
            row = ability(61958, sid)
            self.assertEqual((row["a"], row["b"], row["c"]), (0, 0, 0), f"Echo {sid}")
        # Sanv's Overflowing Hatred starts after 80% and has two windows; Kruul's Mark changes at 25%.
        row = ability(59981, 51204)
        self.assertEqual((row["a"], row["b"], row["c"]), (0, 0, 0))
        row = ability(59981, 51205)
        self.assertEqual((row["a"], row["b"], row["c"]), (0, 45000, 45000))
        self.assertEqual(ability(59991, 51166)["b"], 0)
        # Ground is only the effect-27 spells; Void Zone is a summon and stays unflagged.
        for entry, sid in ((61958, 51113), (61946, 51180), (61946, 51182)):
            self.assertTrue(ability(entry, sid)["ground"], f"{entry} {sid} ground")
        self.assertFalse(ability(59967, 28863)["ground"])
        # Instant spells are not `int`; channels and real casts are.
        self.assertFalse(ability(59967, 28863)["int"])
        self.assertFalse(ability(61958, 51111)["int"])
        self.assertTrue(ability(61946, 51184)["int"])  # channelInterruptFlags & 4
        self.assertTrue(ability(59981, 51204)["int"])  # channel, channelInterruptFlags & 4
        self.assertTrue(ability(59991, 51174)["int"])  # preventionType 1, interruptFlags & 2
        # Echo's whole random list is kickable in the core but must not be kicked (Guardian's Ire stacks).
        for sid in range(51112, 51123):
            if sid in (51115, 51117, 51119):
                continue
            self.assertFalse(ability(61958, sid)["int"], f"Echo {sid} must not be int")
        # The dying piece makes the King cast his own Fury, so it is raid damage around him, not a self buff.
        self.assertEqual(ability(59967, 51229)["target"], "aoe")
        self.assertFalse(ability(59967, 51229)["self"])
        # Each chess piece has its own ability block, so the panel finds it by the engaged entry.
        for entry in (59971, 59970, 59968):
            self.assertIn(51218, {a["sid"] for a in self.bosses[entry]["abilities"]})
            self.assertTrue(ability(entry, 51218)["self"])
        # The boss aura phase rules are visible auras, not passives.
        for entry, sid in ((61939, 51096), (61946, 51176), (61958, 51125), (59967, 51228),
                           (59991, 51173)):
            self.assertTrue(ability(entry, sid)["self"], f"{entry} {sid}")


# Every value below is read off the core row named after it, not copied from the
# CSV: spell_template in sql/base (or the later write in
# sql/database_updates/world/20260512224739_world.sql) and creature_template in
# sql/database_updates/world/20260510092659_world.sql. No Timbermaw creature has
# an EventAI event or a script, so every a/b/c is 0.
# key: (boss, a, b, c, t, aura, self, ground, cone, tail, int, summon, ok, okNeeds,
#       phase, castMs, name)
TIMBERMAW_ABILITIES = {
    49039: (62936, 0, 0, 0, "self", False, True, False, False, False, False, 0, "", {"purge": "nopurge"}, "", 0, "Spirit Bond"),
    49040: (62936, 0, 0, 0, "self", False, True, False, False, False, False, 0, "", {"purge": "nopurge"}, "", 0, "Broken Bond"),
    52806: (2139, 0, 0, 0, "random", True, False, False, False, False, False, 0, "", {}, "", 0, "Venomous Vines"),
    52808: (2139, 0, 0, 0, "aoe", True, False, False, False, False, False, 0, "", {"dispel": "nodispel"}, "", 0, "Aura of Infection"),
    52812: (2139, 0, 0, 0, "aoe", True, False, False, False, False, False, 0, "", {}, "", 0, "Spores of Corruption"),
    52813: (2139, 0, 0, 0, "random", False, False, True, False, False, False, 0, "", {}, "", 0, "Infected Secretion"),
    49027: (62935, 0, 0, 0, "aoe", False, False, True, False, False, False, 0, "", {}, "", 0, "Tremors of Ormanos"),
    49028: (62935, 0, 0, 0, "self", False, False, False, False, False, False, 51608, "", {}, "", 0, "Spawn Tremor of Ormanos"),
    49029: (62935, 0, 0, 0, "victim", False, False, False, False, False, False, 0, "", {}, "", 0, "Rampaging Earth"),
    49030: (62935, 0, 0, 0, "victim", False, False, False, False, False, True, 0, "", {}, "", 0, "Rampaging Earth"),
    49031: (62935, 0, 0, 0, "victim", False, False, False, False, False, False, 0, "", {}, "", 0, "Rampaging Earth"),
    52770: (62940, 0, 0, 0, "random", True, False, False, False, False, False, 0, "", {}, "", 0, "Foulheart Siphon"),
    52763: (62940, 0, 0, 0, "aoe", True, False, False, False, False, False, 0, "", {}, "", 0, "Curse of Foulheart"),
    52790: (62940, 0, 0, 0, "self", False, False, False, False, False, True, 62945, "", {}, "", 0, "Summon Foulheart Imp"),
    34720: (62946, 0, 0, 0, "random", True, False, False, False, False, False, 0, "", {"dispel": "nodispel"}, "", 0, "Corrosive Spit"),
    34721: (62946, 0, 0, 0, "aoe", False, False, False, False, False, False, 0, "", {}, "", 0, "Icicle Crash"),
    34722: (62946, 0, 0, 0, "aoe", False, False, False, False, False, False, 0, "", {}, "", 0, "Icicle Crash"),
    34723: (62946, 0, 0, 0, "random", True, False, False, False, False, False, 0, "", {"dispel": "nodispel"}, "", 0, "Frozen Spit"),
    34767: (62938, 0, 0, 0, "random", True, False, False, False, False, False, 0, "", {"dispel": "nodispel"}, "", 0, "Intruder of the Dream"),
    34741: (62941, 0, 0, 0, "self", False, True, False, False, False, False, 0, "", {"purge": "nopurge"}, "", 0, "Chieftain of Withermaw"),
    34742: (62941, 0, 0, 0, "self", False, False, False, False, False, False, 62943, "", {}, "", 0, "Withermaw Ritual"),
    34748: (62941, 0, 0, 0, "self", False, True, False, False, False, False, 0, "", {"purge": "nopurge"}, "", 0, "Shade of the Withermaw"),
    52788: (62941, 0, 0, 0, "aoe", True, False, False, False, False, False, 0, "", {}, "", 0, "Curse of the Withermaw"),
    52798: (62941, 0, 0, 0, "self", False, False, False, False, False, False, 62886, "", {}, "", 0, "Totem of Corruption"),
}

# boss entry: (adds, phases) read off the same core rows.
# An add is (entry, role, priority); a phase is (name, order, rule).
TIMBERMAW_BOSSES = {
    62934: ((), (("fight", 1, {"kind": "start"}),)),
    62936: (((62937, "linked", 1),), (("fight", 1, {"kind": "start"}),)),
    62937: ((), (("fight", 1, {"kind": "start"}),)),
    2139: ((), (("fight", 1, {"kind": "start"}),)),
    62935: (((51608, "summon", 0),), (("fight", 1, {"kind": "start"}),)),
    62940: (((62945, "summon", 0),), (("fight", 1, {"kind": "start"}),)),
    62946: ((), (("fight", 1, {"kind": "start"}),)),
    62938: ((), (("fight", 1, {"kind": "start"}),)),
    60699: ((), (("fight", 1, {"kind": "start"}),)),
    62941: (((62943, "summon", 0), (62886, "totem", 0)), (("fight", 1, {"kind": "start"}),)),
}


class TimbermawValueTest(unittest.TestCase):
    """Pin every Timbermaw ability, add and phase rule to its core row."""

    def setUp(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        self.bosses = {
            boss["entry"]: boss
            for boss in doc["bosses"].values()
            if boss["map"] == "timbermaw_hold"
        }
        self.rows = rows

    def test_boss_set(self):
        self.assertEqual(set(self.bosses), set(TIMBERMAW_BOSSES))

    def test_every_ability_against_its_source(self):
        built = {}
        for boss in self.bosses.values():
            for ability in boss["abilities"]:
                built[ability["sid"]] = (
                    boss["entry"],
                    ability["a"],
                    ability["b"],
                    ability["c"],
                    ability["target"],
                    ability["aura"],
                    ability["self"],
                    ability["ground"],
                    ability["cone"],
                    ability["tail"],
                    ability["int"],
                    ability["summon"],
                    ability["ok"],
                    ability["okNeeds"],
                    ability["phase"],
                    ability["castMs"],
                    ability["name"],
                )
        self.assertEqual(built, TIMBERMAW_ABILITIES)

    def test_adds_and_phases(self):
        for entry, (adds, phases) in TIMBERMAW_BOSSES.items():
            boss = self.bosses[entry]
            self.assertEqual(
                [(add["entry"], add["role"], add["priority"]) for add in boss["adds"]],
                list(adds),
                entry,
            )
            self.assertEqual(
                [(phase["name"], phase["order"], phase["rule"]) for phase in boss["phases"]],
                list(phases),
                entry,
            )

    def test_no_timer_or_later_phase_is_invented(self):
        for boss in self.bosses.values():
            for ability in boss["abilities"]:
                self.assertEqual((ability["a"], ability["b"], ability["c"]), (0, 0, 0), ability)
                self.assertEqual(ability["phase"], "", ability)
        for boss in self.bosses.values():
            self.assertEqual(
                [phase["rule"]["kind"] for phase in boss["phases"]],
                ["start"],
                boss["entry"],
            )

    def test_area_and_interrupt_choices(self):
        ground = {sid for sid, v in TIMBERMAW_ABILITIES.items() if v[7]}
        self.assertEqual(ground, {49027, 52813})
        interrupt = {sid for sid, v in TIMBERMAW_ABILITIES.items() if v[10]}
        self.assertEqual(interrupt, {49030, 52790})
        self.assertFalse(any(v[8] or v[9] for v in TIMBERMAW_ABILITIES.values()))

    def test_every_timbermaw_citation_resolves(self):
        for row in self.rows:
            if row["map"] != "timbermaw_hold":
                continue
            for part in row["source"].split(";"):
                if part.startswith("kb:"):
                    continue
                if part.startswith("data/"):
                    self.assertTrue((ROOT / part.split(":")[0]).is_file(), (row["_line"], part))
                    continue
                self.assertTrue(part.startswith("sql/"), (row["_line"], part))
                self.assertNotIn("src/scripts", part)


class EmeraldSanctumValueTest(unittest.TestCase):
    """Every Emerald Sanctum number and flag, read off the core scripts (not the CSV).

    Source: src/scripts/dungeons/emerald_sanctum/{boss_solnius.cpp,instance_emerald_sanctum.cpp}.
    """

    ABILITIES = {
        60747: {  # Erennius
            24099: dict(a=0, b=9000, c=13000, target="aoe", phase="", castMs=0, flags={"aura": True}),
            56506: dict(a=37000, b=30000, c=33000, target="aoe", phase="", castMs=0,
                        flags={"aura": True, "okNeeds": {"dispel": "nodispel"}}),
            46079: dict(a=7000, b=7000, c=7000, target="random", phase="", castMs=0,
                        flags={"aura": True, "okNeeds": {"dispel": "nodispel"}}),
            46080: dict(a=0, b=35000, c=35000, target="aoe", phase="", castMs=0,
                        flags={"aura": True, "int": True}),
            46082: dict(a=0, b=70000, c=70000, target="ally", phase="", castMs=0, flags={"ok": "script"}),
            56505: dict(a=0, b=0, c=0, target="victim", phase="", castMs=0, flags={"aura": True, "hp": 50}),
        },
        60748: {  # Solnius
            56507: dict(a=6000, b=6000, c=6000, target="victim", phase="p1", castMs=0, flags={"aura": True}),
            56508: dict(a=0, b=0, c=0, target="aoe", phase="", castMs=0, flags={"aura": True}),
            24839: dict(a=0, b=15000, c=15000, target="victim", phase="p2", castMs=0, flags={"cone": True}),
            56510: dict(a=0, b=27000, c=27000, target="aoe", phase="p2", castMs=0, flags={"aura": True}),
            56512: dict(a=0, b=27000, c=27000, target="random", phase="p2", castMs=0, flags={"aura": True}),
            56514: dict(a=0, b=27000, c=27000, target="random", phase="p2", castMs=0, flags={"aura": True}),
            56516: dict(a=0, b=27000, c=27000, target="random", phase="p2", castMs=0, flags={"aura": True}),
            46079: dict(a=6000, b=6000, c=6000, target="random", phase="", castMs=0,
                        flags={"aura": True, "okNeeds": {"dispel": "nodispel"}}),
            46081: dict(a=17000, b=17000, c=17000, target="aoe", phase="", castMs=0, flags={}),
            25148: dict(a=0, b=0, c=0, target="self", phase="", castMs=0, flags={"self": True, "ok": "script"}),
        },
        60742: {  # Sanctum Dreamer
            56500: dict(a=10000, b=10000, c=10000, target="random", phase="", castMs=0, flags={"aura": True}),
        },
        60743: {  # Sanctum Dragonkin
            27564: dict(a=10000, b=10000, c=10000, target="self", phase="", castMs=0, flags={"self": True}),
        },
        60745: {  # Sanctum Wyrmkin
            26050: dict(a=7000, b=7000, c=7000, target="victim", phase="", castMs=0, flags={"aura": True}),
            56503: dict(a=25000, b=21000, c=21000, target="aoe", phase="", castMs=0, flags={"aura": True}),
        },
        60746: {  # Sanctum Scalebane
            19983: dict(a=6000, b=6000, c=6000, target="victim", phase="", castMs=0, flags={}),
            56504: dict(a=25000, b=25000, c=25000, target="aoe", phase="", castMs=0, flags={"aura": True}),
        },
        61212: {  # Sanctum Supressor
            56501: dict(a=15000, b=14000, c=14000, target="random", phase="", castMs=0, flags={"aura": True}),
        },
    }

    PHASES = {
        60747: [],
        60748: [
            ("p2", {"kind": "hp", "hp": 90}, 1),
            ("p1", {"kind": "start"}, 2),
        ],
    }

    ADDS = {
        60747: {},
        60748: {60742: ("summon", 1), 60743: ("summon", 2), 60744: ("linked", 2),
                60745: ("summon", 2), 60746: ("summon", 2), 61212: ("summon", 1)},
    }

    @classmethod
    def setUpClass(cls):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        cls.doc = boss_data.build_document(rows, names)

    def boss(self, entry):
        boss = self.doc["bosses"].get(str(entry))
        self.assertIsNotNone(boss, f"boss {entry} is missing from the table")
        return boss

    def test_ability_values(self):
        for entry, expected in self.ABILITIES.items():
            got = {ability["sid"]: ability for ability in self.boss(entry)["abilities"]}
            self.assertEqual(sorted(got), sorted(expected), f"boss {entry} ability set")
            for sid, want in expected.items():
                ability = got[sid]
                label = f"boss {entry} spell {sid} ({ability['name']})"
                self.assertEqual((ability["a"], ability["b"], ability["c"]),
                                 (want["a"], want["b"], want["c"]), label + " timers")
                self.assertEqual(ability["target"], want["target"], label + " target")
                self.assertEqual(ability["phase"], want["phase"], label + " phase")
                self.assertEqual(ability["castMs"], want["castMs"], label + " castMs")
                for flag, value in want["flags"].items():
                    self.assertEqual(ability[flag], value, f"{label} flag {flag}")

    def test_phase_rules(self):
        for entry, expected in self.PHASES.items():
            got = [(phase["name"], phase["rule"], phase["order"])
                   for phase in self.boss(entry)["phases"]]
            self.assertEqual(got, expected, f"boss {entry} phases")

    def test_adds(self):
        for entry, expected in self.ADDS.items():
            got = {add["entry"]: (add["role"], add["priority"]) for add in self.boss(entry)["adds"]}
            self.assertEqual(got, expected, f"boss {entry} adds")

    def test_portal_add_blocks(self):
        for entry in (60742, 60743, 60745, 60746, 61212):
            boss = self.boss(entry)
            self.assertEqual(boss["map"], "emerald_sanctum")
            self.assertTrue(boss["abilities"], f"portal add {entry} has no ability block")
        self.assertNotIn("60744", self.doc["bosses"], "Sanctum Wyrm stays trash, not a boss block")

    def test_solid_semantics(self):
        solnius = self.boss(60748)
        abilities = {ability["sid"]: ability for ability in solnius["abilities"]}
        erennius = {ability["sid"]: ability for ability in self.boss(60747)["abilities"]}
        # The sleep is an aura (25148) inside phase 2, not a phase: the threat list survives GoSleep and the
        # phase-2 casts keep running, so no phase row matches the aura and no ability is tagged to a sleep phase.
        self.assertEqual(solnius["phases"][0]["rule"], {"kind": "hp", "hp": 90})
        self.assertNotIn("sleep", [phase["name"] for phase in solnius["phases"]])
        self.assertEqual(abilities[25148]["ok"], "script")
        self.assertFalse([a["phase"] for a in abilities.values() if a["phase"] not in ("", "p1", "p2")],
                         "no ability is tagged to a sleep phase")
        # Emerald Rot runs in both phases (15 s in p1, 30 s below 40% in p2) and shares the timer with the wake
        # reset, so it carries no phase tag and no column timer. It is an area around Solnius, not a player debuff.
        self.assertEqual(abilities[56508]["phase"], "")
        self.assertEqual((abilities[56508]["a"], abilities[56508]["b"], abilities[56508]["c"]), (0, 0, 0))
        self.assertEqual(abilities[56508]["target"], "aoe")
        # Bane of Eranikus is the other area spell; the three player debuffs share its literal 27 s timer.
        self.assertEqual(abilities[56510]["target"], "aoe")
        self.assertEqual((abilities[56510]["b"], abilities[56510]["c"]), (27000, 27000))
        self.assertEqual({abilities[sid]["b"] for sid in (56512, 56514, 56516)}, {27000})
        # Acid Breath counts only from the transform: first column 0, 15 s repeats.
        self.assertEqual(abilities[24839]["a"], 0)
        # Only Wail of Erennius is kickable: preventionType 1 and interruptFlags 0x02 with castingTimeIndex 171.
        self.assertTrue(erennius[46080]["int"])
        self.assertFalse({aid for aid, a in erennius.items() if a["int"]} - {46080})
        self.assertFalse({aid for aid, a in abilities.items() if a["int"]}, "no Solnius cast is kickable")
        # Call of Nightmare is one spell id on both bosses, with its own timer on each.
        self.assertEqual((erennius[46079]["a"], erennius[46079]["b"]), (7000, 7000))
        self.assertEqual((abilities[46079]["a"], abilities[46079]["b"]), (6000, 6000))


# ---- Blackrock dungeons (BRD 230, LBRS 229) ---------------------------------------------------------------------------
# Read off the core scripts, not off the CSV: every ability with its timer window, target and flags, every phase rule and
# every add. A row that changes silently (or loses a flag) fails here even though counts and the JSON still agree.
BLACKROCK_ABILITIES = {
    # boss, spell: (a, b, c, target, phase, flags)
    (9018, 14032): (4000, 7000, 7000, "random", "", {"aura"}),
    (9018, 14033): (14000, 10000, 10000, "random", "", {"int"}),
    (9018, 13704): (32000, 30000, 30000, "aoe", "", {"aura", "dispel-nodispel"}),
    (9018, 12040): (8000, 25000, 25000, "self", "", {"self"}),
    (9319, 21049): (0, 0, 0, "self", "", {"self"}),
    (9319, 13730): (0, 0, 0, "aoe", "", {"aura", "dispel-nodispel"}),
    (9319, 15615): (0, 0, 0, "victim", "", {"tank"}),
    (9016, 13895): (0, 0, 0, "random", "", {"int", "interrupt-nokick", "summon=9436"}),
    (9016, 13880): (0, 0, 0, "victim", "", {"aura", "dispel-nodispel"}),
    (8894, 15586): (0, 0, 0, "ally", "", {"int"}),
    (8894, 10961): (0, 0, 0, "ally", "", {"int"}),
    (8894, 10947): (0, 0, 0, "victim", "", {"int"}),
    (9033, 15572): (0, 5000, 15000, "victim", "", {"aura", "dispel-nodispel"}),
    (8983, 15507): (0, 0, 0, "self", "", {"self"}),
    (8983, 15305): (5000, 14000, 14000, "victim", "", {"int"}),
    (8983, 15605): (2000, 6000, 6000, "victim", "", {"tank"}),
    (9056, 8258): (1000, 240000, 240000, "self", "", {"self", "purge-nopurge"}),
    (9056, 13953): (0, 6000, 8500, "victim", "", {"tank"}),
    (9056, 15493): (0, 0, 0, "self", "", {"int", "hp60"}),
    (9056, 15346): (0, 0, 0, "self", "", {"self", "hp20"}),
    (9056, 11978): (0, 0, 0, "victim", "", {"tank"}),
    (9156, 14744): (0, 0, 0, "self", "", {"self", "purge-nopurge"}),
    (9017, 26977): (0, 0, 0, "aoe", "", {"aura"}),
    (9017, 13900): (0, 0, 0, "aoe", "", {"int"}),
    (9017, 14099): (0, 0, 0, "random", "", {"heal"}),
    (9017, 13899): (0, 0, 0, "random", "", {"ground", "int"}),
    (9938, 13900): (5000, 6000, 6000, "aoe", "", {"int"}),
    (9938, 24375): (0, 8000, 8000, "aoe", "stomp", {"aura", "dispel-nodispel"}),
    (9019, 17492): (0, 10000, 15000, "victim", "", {"aura", "dispel-nodispel"}),
    (9019, 15636): (18000, 18000, 18000, "self", "", {"self", "purge-nopurge"}),
    (9196, 24375): (15000, 14000, 14000, "aoe", "", {"aura", "dispel-nodispel"}),
    (9196, 18368): (10000, 10000, 10000, "victim", "", {"tank"}),
    (9196, 18106): (14000, 18000, 18000, "victim", "", {"aura", "dispel-nodispel"}),
    (9196, 24317): (2000, 25000, 25000, "victim", "", {"aura", "dispel-nodispel"}),
    (9196, 20686): (18000, 12000, 12000, "aoe", "", {"heal"}),
    (9196, 22356): (24000, 18000, 18000, "aoe", "", {"aura"}),
    (9236, 24673): (2000, 45000, 45000, "aoe", "", {"aura"}),
    (9236, 16708): (8000, 15000, 15000, "random", "", {"aura"}),
    (9236, 20691): (14000, 7000, 7000, "victim", "", {"tank"}),
    (9237, 15618): (8000, 6000, 6000, "victim", "", {"aura", "dispel-nodispel"}),
    (9237, 15284): (14000, 12000, 12000, "victim", "", {"tank"}),
    (9237, 10966): (20000, 14000, 14000, "victim", "", {"tank"}),
    (9237, 15708): (12000, 10000, 10000, "victim", "", {"aura", "dispel-nodispel"}),
    (9237, 15615): (32000, 16000, 16000, "victim", "", {"tank"}),
    (9237, 16075): (0, 5000, 15000, "victim", "",
                    {"int", "interrupt-nokick", "aura", "dispel-nodispel"}),
    (10596, 16468): (0, 7000, 15000, "random", "", {"aura"}),
    (10596, 16104): (0, 0, 0, "victim", "", {"cone", "aura"}),
    (10596, 16103): (0, 0, 0, "self", "", {"summon=10375"}),
    (9736, 16496): (1000, 1750, 1750, "victim", "", {"tank"}),
    (9736, 16497): (16000, 14000, 14000, "aoe", "",
                    {"int", "interrupt-nokick", "aura", "dispel-nodispel"}),
    (10220, 10887): (8000, 14000, 14000, "aoe", "", {"heal"}),
    (10220, 14099): (14000, 10000, 10000, "victim", "", {"tank"}),
    (10268, 16495): (0, 0, 0, "victim", "", {"tank"}),
    (10268, 16128): (0, 0, 0, "victim", "", {"aura"}),
    (10268, 8269): (0, 0, 0, "self", "", {"self", "hp20"}),
    (9568, 11130): (20000, 20000, 20000, "aoe", "", {"heal"}),
    (9568, 23511): (2000, 10000, 10000, "aoe", "", {"aura", "dispel-nodispel"}),
    (9568, 20691): (6000, 7000, 7000, "victim", "", {"tank"}),
    (9568, 20686): (12000, 14000, 14000, "aoe", "", {"heal"}),
    (9216, 16171): (0, 0, 0, "self", "", {"script"}),
    (9268, 13446): (0, 0, 0, "victim", "", {"tank"}),
    (9268, 17547): (0, 0, 0, "victim", "", {"aura", "dispel-nodispel"}),
}

BLACKROCK_PHASES = {
    9938: [("stomp", "hp", 51, 1), ("fight", "start", None, 2)],
}

BLACKROCK_ADDS = {
    9016: [(9436, "summon", 1)],
    9033: [(8894, "summon", 1), (8901, "summon", 2)],
    8983: [(8906, "linked", 0), (8907, "linked", 0)],
    9156: [(9178, "summon", 1)],
    9019: [(8929, "linked", 0)],
    10220: [(10268, "summon", 1)],
    9568: [(9216, "summon", 1), (9268, "summon", 2)],
    10596: [(10375, "summon", 1)],
}
BLACKROCK_MAPS = ("blackrock_depths", "upper_blackrock_spire")


def _flag_set(ability):
    """The semantic flags of a built ability, in the fixture's spelling."""
    flags = set()
    for name in ("aura", "self", "int", "mark", "ground", "cone", "tail"):
        if ability[name]:
            flags.add(name)
    if ability["summon"]:
        flags.add("summon=%d" % ability["summon"])
    if ability["hp"]:
        flags.add("hp%d" % ability["hp"])
    if ability["ok"]:
        flags.add(ability["ok"])
    for need, reason in ability["okNeeds"].items():
        flags.add("%s-%s" % (need, reason))
    return flags


class DungeonBlackrockValueTest(unittest.TestCase):
    """Every BRD and LBRS value the panel draws, pinned to what the core source says."""

    @classmethod
    def setUpClass(cls):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        cls.doc = boss_data.build_document(rows, names)
        cls.by_boss = cls.doc["bosses"]

    def test_ability_values(self):
        seen = set()
        for key, (a, b, c, target, phase, flags) in BLACKROCK_ABILITIES.items():
            boss, sid = key
            abilities = {x["sid"]: x for x in self.by_boss[str(boss)]["abilities"]}
            self.assertIn(sid, abilities, "boss %d lost spell %d" % (boss, sid))
            ability = abilities[sid]
            self.assertEqual((ability["a"], ability["b"], ability["c"]), (a, b, c), key)
            self.assertEqual(ability["target"], target, key)
            self.assertEqual(ability["phase"], phase, key)
            self.assertEqual(ability["castMs"], 0, key)
            self.assertEqual(_flag_set(ability), flags, key)
            seen.add(key)
        built = {
            (int(entry), ability["sid"])
            for entry, boss in self.by_boss.items()
            for ability in boss["abilities"]
            if boss["map"] in BLACKROCK_MAPS
        }
        self.assertEqual(built, seen, "the two Blackrock maps carry exactly the pinned abilities")

    def test_phase_rules(self):
        for entry, expected in BLACKROCK_PHASES.items():
            phases = self.by_boss[str(entry)]["phases"]
            got = []
            for phase in phases:
                rule = phase["rule"]
                got.append((phase["name"], rule["kind"], rule.get("hp"), phase["order"]))
            self.assertEqual(got, expected, entry)

    def test_adds(self):
        for entry, expected in BLACKROCK_ADDS.items():
            got = [(a["entry"], a["role"], a["priority"]) for a in self.by_boss[str(entry)]["adds"]]
            self.assertEqual(got, expected, entry)

    def test_creature_spell_lists_reach_the_panel(self):
        # Incendius has no ai_name or script_name, but the default AggressorAI still casts his list.
        self.assertEqual([a["sid"] for a in self.by_boss["9017"]["abilities"]],
                         [26977, 13900, 14099, 13899])
        self.assertEqual(self.by_boss["9017"]["adds"], [])
        # Mother Smolderweb's C++ script is not assigned; EventAI runs her list and Mother's Milk.
        self.assertEqual([a["sid"] for a in self.by_boss["10596"]["abilities"]],
                         [16468, 16104, 16103])
        self.assertEqual([(a["entry"], a["role"]) for a in self.by_boss["10596"]["adds"]],
                         [(10375, "summon")])

    def test_magmus_war_stomp_only_in_the_stomp_phase(self):
        magmus = self.by_boss["9938"]
        stomp = next(a for a in magmus["abilities"] if a["sid"] == 24375)
        burst = next(a for a in magmus["abilities"] if a["sid"] == 13900)
        self.assertEqual(stomp["phase"], "stomp")
        self.assertEqual(burst["phase"], "")
        self.assertEqual([p["rule"] for p in magmus["phases"]],
                         [{"kind": "hp", "hp": 51}, {"kind": "start"}])


if __name__ == "__main__":
    unittest.main(verbosity=2)
