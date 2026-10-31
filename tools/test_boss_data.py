"""Gate tests for tools/boss_data.py and the committed boss table.

Covers the four failing cases the fd-boss spec names for the data task:
a timer that does not match its cited line, a duplicate spell/add entry, a CSV
edit without a rebuilt JSON, and the --db existence check. The fixture scripts
are tiny fake C++ files so the citation checks run without a core checkout.
"""

import argparse
import csv
import json
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

    def test_committed_data_builds_to_committed_json(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names)
        committed = boss_data.DEFAULT_JSON.read_text(encoding="utf-8")
        self.assertEqual(committed, boss_data.dump_json(doc))
        self.assertEqual(len(doc["bosses"]), 35)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 223)

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
