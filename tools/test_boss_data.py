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
        self.assertEqual(len(doc["bosses"]), 25)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 166)

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
        51182: kara_ability(target="victim", ground=True),
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
        51112: kara_ability(target="random", aura=True, int=True, okNeeds={"dispel": "nodispel"}),
        51113: kara_ability(target="random", ground=True, int=True),
        51114: kara_ability(target="aoe", aura=True, int=True),
        51116: kara_ability(target="random", aura=True, int=True),
        51118: kara_ability(target="random", aura=True, int=True),
        51120: kara_ability(target="random", int=True),
        51121: kara_ability(target="random", int=True),
        51122: kara_ability(target="random", int=True, summon=59994),
        51125: kara_ability(target="self", self=True, ok="phase"),
        8269: kara_ability(target="self", self=True, hp=20, ok="phase"),
        51110: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51109: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59967: {  # King, the chess event
        28863: kara_ability(a=20000, b=20000, c=20000, target="random"),
        51230: kara_ability(b=8000, c=14000, target="self", summon=59972),
        51231: kara_ability(b=40000, c=50000, target="aoe", aura=True),
        51228: kara_ability(target="self", self=True, ok="phase"),
        51229: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
    },
    59981: {  # Sanv Tas'dal
        51194: kara_ability(a=5000, b=13000, c=13000, target="random", aura=True),
        51195: kara_ability(a=3000, b=11000, c=11000, target="random", aura=True),
        51196: kara_ability(a=7000, b=15000, c=15000, target="aoe", aura=True),
        51198: kara_ability(a=1500, b=5500, c=5500, target="random", aura=True, int=True),
        51204: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51205: kara_ability(b=45000, c=45000, target="aoe", ok="heal"),
        51209: kara_ability(target="self", hp=50, ok="phase"),
        51206: kara_ability(target="self", self=True, hp=20, ok="phase"),
        51207: kara_ability(target="self", self=True, hp=20, ok="phase"),
    },
    59991: {  # Kruul
        51164: kara_ability(a=2000, b=4000, c=4000, target="victim", ok="tank"),
        51166: kara_ability(a=30000, target="aoe", aura=True),
        51170: kara_ability(a=25000, target="random", summon=59990),
        51172: kara_ability(target="aoe", hp=25, ok="phase"),
        51173: kara_ability(target="self", self=True, okNeeds={"purge": "nopurge"}),
        51174: kara_ability(b=10000, c=10000, target="aoe", ok="heal"),
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
        self.assertTrue(ability(61958, 51112)["int"])  # castingTimeIndex 130, preventionType 1
        # The boss aura phase rules are visible auras, not passives.
        for entry, sid in ((61939, 51096), (61946, 51176), (61958, 51125), (59967, 51228),
                           (59991, 51173)):
            self.assertTrue(ability(entry, sid)["self"], f"{entry} {sid}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
