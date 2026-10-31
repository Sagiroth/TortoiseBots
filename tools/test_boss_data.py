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
        self.assertEqual(len(doc["bosses"]), 38)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 174)

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
