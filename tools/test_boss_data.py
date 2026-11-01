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
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import boss_data  # noqa: E402

# The committed table; BOSS_DATA_CSV points the value tests at another revision's table.
BOSS_CSV = Path(os.environ.get("BOSS_DATA_CSV", ROOT / "data" / "boss" / "boss_abilities.csv"))

# The maps only the trash table has a mob in: raids.csv lists them with no boss of their own, so every boss build
# here hands them to make_bosses, which never reads the trash CSV by itself.
TRASH_MAPS = boss_data.trash_map_keys()

CORE = [
    "#define SPELL_A 24328",
    "#define NPC_ADD 14884",
    "void Reset() { A_Timer = 15000; }",
    "void UpdateAI() { A_Timer = urand(14000, 16000); }",
    "SummonCreature(NPC_ADD, x);",
    "if (GetHealthPercent() < 50) m_bPhaseTwo = true;",
    "#define NPC_TRASH 14750",
]

NAMES = {
    "spells": {"24328": {"name": "Corrupted Blood", "castMs": 0}},
    "creatures": {"14517": {"name": "High Priestess Jeklik"}, "14884": {"name": "Parasitic Serpent"}},
}

HEADER = ["map", "boss", "row", "key", "a", "b", "c", "d", "source"]
TRASH_HEADER = ["map", "entry", "row", "key", "a", "b", "c", "d", "source"]
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

# The trash table: one `mob` row per creature entry, its `ability` rows (spell id, no timers, the boss flags plus
# `r=`), then `note` rows (prose, no citation to give).
TRASH_NAMES = {
    "spells": {
        "24328": {"name": "Corrupted Blood", "castMs": 0},
        "14884": {"name": "Parasitic Serpent", "castMs": 0},
    },
    "creatures": {
        "14517": {"name": "High Priestess Jeklik"},
        "14884": {"name": "Parasitic Serpent"},
        "14750": {"name": "Gurubashi Bat Rider"},
    },
}


def trash_mob(entry="14750", danger="3", roles="aoe", kill="3", **kw):
    row = {
        "map": "zulgurub",
        "entry": entry,
        "row": "mob",
        "key": "",
        "a": danger,
        "b": roles,
        "c": kill,
        "d": "",
        "source": "boss.cpp:7",
    }
    row.update(kw)
    return row


def trash_ability(entry="14750", key="24328", flags="t=aoe;r=10", **kw):
    row = {
        "map": "zulgurub",
        "entry": entry,
        "row": "ability",
        "key": key,
        "a": "0",
        "b": "0",
        "c": "0",
        "d": flags,
        "source": "boss.cpp:1",
    }
    row.update(kw)
    return row


def trash_note(entry="14750", text="two wipes at the pull", source="", **kw):
    row = {
        "map": "zulgurub",
        "entry": entry,
        "row": "note",
        "key": "",
        "a": text,
        "b": "",
        "c": "",
        "d": "",
        "source": source,
    }
    row.update(kw)
    return row


BASE_TRASH = [trash_mob(), trash_ability(), trash_note()]


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
    def __init__(self, rows=None, names=None, raids=None, answers=None, trash=None):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.csv = self.dir / "boss_abilities.csv"
        self.json = self.dir / "boss_abilities.json"
        self.trash_csv = self.dir / "trash_abilities.csv"
        self.trash_json = self.dir / "trash_abilities.json"
        self.names = self.dir / "names.json"
        self.raids = self.dir / "raids.csv"
        self.answers = self.dir / "code_answers.csv"
        (self.dir / "core").mkdir()
        (self.dir / "core" / "boss.cpp").write_text("\n".join(CORE) + "\n")
        self.write(rows if rows is not None else [dict(r) for r in BASE_ROWS],
                   names if names is not None else NAMES)
        self.write_raids(raids if raids is not None else [["zulgurub", "309", "Zul'Gurub"]])
        self.write_answers(answers if answers is not None else [])
        self.write_trash(trash if trash is not None else [])

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

    def write_trash(self, rows):
        with open(self.trash_csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f, lineterminator="\n")
            writer.writerow(TRASH_HEADER)
            for row in rows:
                writer.writerow([row[c] for c in TRASH_HEADER])
        self.trash_rows = rows

    def rebuild(self):
        raids = boss_data.read_raids(self.raids)
        names = boss_data.read_names(self.names)
        bosses, _ = boss_data.boss_entry_sets(boss_data.read_rows(self.csv))
        trash_doc = boss_data.build_trash_document(
            boss_data.read_trash_rows(self.trash_csv), names, raids, bosses
        )
        doc = boss_data.build_document(
            boss_data.read_rows(self.csv),
            names,
            raids,
            boss_data.read_code_answers(self.answers),
            self.dir,
            extra_maps=[m["key"] for m in trash_doc["maps"]],
        )
        self.json.write_text(boss_data.dump_json(doc))
        self.trash_json.write_text(boss_data.dump_trash_json(trash_doc))

    def args(self, db=False):
        return argparse.Namespace(
            csv=str(self.csv),
            json=str(self.json),
            trash_csv=str(self.trash_csv),
            trash_json=str(self.trash_json),
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
            rows, names, boss_data.read_raids(boss_data.DEFAULT_RAIDS), extra_maps=TRASH_MAPS
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

    def test_an_empty_source_fails(self):
        rows = [dict(r) for r in BASE_ROWS]
        rows.append({"map": "zulgurub", "boss": "14517", "row": "note", "key": "", "a": "a note",
                     "b": "", "c": "", "d": "", "source": ""})
        errors, _ = self.make(rows).validate()
        self.assertTrue(any("empty source" in e for e in errors), errors)

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
        doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
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
        doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
        committed = boss_data.DEFAULT_JSON.read_text(encoding="utf-8")
        self.assertEqual(committed, boss_data.dump_json(doc))
        self.assertEqual(len(doc["bosses"]), 148)
        self.assertEqual(sum(len(b["abilities"]) for b in doc["bosses"].values()), 705)

    def test_every_panel_name_is_present(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
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
        return boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)

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


class TrashDataTest(unittest.TestCase):
    """The trash table's own rules: one `mob` row per entry, the boss flag grammar plus `r=`, the closed role set,
    no timer columns, the shared-entry rule against the boss table and the committed JSON's freshness."""

    def make(self, trash=None, names=None, raids=None, rebuild=True):
        rows = [dict(r) for r in BASE_TRASH] if trash is None else trash
        fx = Fixture(None, names or TRASH_NAMES, raids, None, rows)
        self.addCleanup(fx.tmp.cleanup)
        if rebuild:
            fx.rebuild()
        return fx

    def errors(self, trash=None, names=None, raids=None):
        """The validate() errors of a table that is deliberately wrong: a table that cannot be built is left
        unbuilt, so validate() reports the build error instead of a stale JSON."""
        fx = self.make(trash, names, raids, rebuild=False)
        try:
            fx.rebuild()
        except boss_data.DataError:
            pass
        return fx.validate()[0]

    def test_valid_fixture_passes(self):
        self.assertEqual([], self.errors())

    def test_danger_out_of_range_fails(self):
        errors = self.errors([trash_mob(danger="4"), trash_ability(), trash_note()])
        self.assertTrue(any("danger 4" in e for e in errors), errors)

    def test_kill_out_of_range_fails(self):
        errors = self.errors([trash_mob(kill="4"), trash_ability(), trash_note()])
        self.assertTrue(any("kill 4" in e for e in errors), errors)

    def test_a_role_outside_the_closed_set_fails(self):
        errors = self.errors([trash_mob(roles="aoe;tank"), trash_ability(), trash_note()])
        self.assertTrue(any("role 'tank'" in e for e in errors), errors)

    def test_a_duplicate_role_fails(self):
        errors = self.errors([trash_mob(roles="aoe;aoe"), trash_ability(), trash_note()])
        self.assertTrue(any("duplicate role" in e for e in errors), errors)

    def test_a_mob_with_no_role_fails(self):
        errors = self.errors([trash_mob(roles=""), trash_ability(), trash_note()])
        self.assertTrue(any("at least one role" in e for e in errors), errors)

    def test_a_timer_column_fails(self):
        errors = self.errors([trash_mob(), trash_ability(a="8000"), trash_note()])
        self.assertTrue(any("is a timer" in e for e in errors), errors)

    def test_a_radius_out_of_range_fails(self):
        errors = self.errors([trash_mob(), trash_ability(flags="t=aoe;r=61"), trash_note()])
        self.assertTrue(any("r=61" in e for e in errors), errors)

    def test_a_zero_radius_fails(self):
        # The spec's radius is 1-60 yards: a row that says r=0 says nothing, so it is refused rather than read as "no radius".
        errors = self.errors([trash_mob(), trash_ability(flags="t=aoe;r=0"), trash_note()])
        self.assertTrue(any("r=0" in e and "outside 1-60" in e for e in errors), errors)

    def test_a_radius_is_no_boss_flag(self):
        with self.assertRaises(boss_data.DataError):
            boss_data.parse_flags("t=aoe;r=10")

    def test_the_db_check_reads_a_trash_radius_row(self):
        # The fixture's trash ability carries r=10, and the DB check parses every ability row of both tables: a trash
        # row must not stop it (the flag parser used to read r= as an unknown flag and throw on the first one).
        fx = self.make()
        original = boss_data.db_connect
        boss_data.db_connect = lambda args: FakeConn(spells={24328}, creatures={14884, 14750})
        try:
            errors, _ = fx.validate(db=True)
        finally:
            boss_data.db_connect = original
        self.assertEqual([], errors)

    def test_a_duplicate_ability_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_ability(), trash_note()])
        self.assertTrue(any("duplicate ability" in e for e in errors), errors)

    def test_an_ability_before_its_mob_row_fails(self):
        errors = self.errors([trash_ability(), trash_mob(), trash_note()])
        self.assertTrue(any("must follow its mob row" in e for e in errors), errors)

    def test_an_ability_under_another_entry_fails(self):
        errors = self.errors([trash_mob(), trash_ability(entry="14751"), trash_note()])
        self.assertTrue(any("must follow its mob row" in e for e in errors), errors)

    def test_an_unknown_row_kind_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note(), trash_note(text="x") | {"row": "phase"}])
        self.assertTrue(any("unknown trash row kind" in e for e in errors), errors)

    def test_a_boss_entry_fails(self):
        errors = self.errors([trash_mob(entry="14517"), trash_ability(entry="14517")])
        self.assertTrue(any("is a boss of the boss table" in e for e in errors), errors)

    def test_a_boss_add_is_allowed(self):
        # 14884 is Jeklik's summon add in the boss table; its own abilities are the trash block's job.
        errors = self.errors(
            [
                trash_mob(entry="14884", source="boss.cpp:2"),
                trash_ability(entry="14884"),
                trash_note(entry="14884"),
            ]
        )
        self.assertEqual([], errors)

    def test_a_map_without_a_raids_row_fails(self):
        errors = self.errors([trash_mob(map="scholomance"), trash_ability(map="scholomance"), trash_note(map="scholomance")])
        self.assertTrue(any("no row in raids.csv" in e for e in errors), errors)

    def test_a_map_that_only_the_trash_table_uses_is_allowed(self):
        errors = self.errors(
            [trash_mob(map="scholomance"), trash_ability(map="scholomance"), trash_note(map="scholomance")],
            raids=[["zulgurub", "309", "Zul'Gurub"], ["scholomance", "289", "Scholomance"]],
        )
        self.assertEqual([], errors)

    def test_a_missing_creature_name_fails(self):
        names = json.loads(json.dumps(TRASH_NAMES))
        del names["creatures"]["14750"]
        errors = self.errors([trash_mob(), trash_ability(), trash_note()], names=names)
        self.assertTrue(any("no creature name for 14750" in e for e in errors), errors)

    def test_a_missing_spell_name_fails(self):
        names = json.loads(json.dumps(TRASH_NAMES))
        del names["spells"]["24328"]
        errors = self.errors([trash_mob(), trash_ability(), trash_note()], names=names)
        self.assertTrue(any("no spell name for 24328" in e for e in errors), errors)

    def test_a_summon_needs_a_name(self):
        errors = self.errors([trash_mob(), trash_ability(flags="summon=15141"), trash_note()])
        self.assertTrue(any("no creature name for summon 15141" in e for e in errors), errors)

    def test_a_note_column_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note(text="x") | {"b": "1"}])
        self.assertTrue(any("a note row uses only a and source" in e for e in errors), errors)

    def test_an_empty_note_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note(text="")])
        self.assertTrue(any("note text is empty" in e for e in errors), errors)

    def test_a_mob_row_with_a_key_fails(self):
        errors = self.errors([trash_mob(key="24328"), trash_ability(), trash_note()])
        self.assertTrue(any("a mob row has no key" in e for e in errors), errors)

    def test_a_note_row_with_a_key_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note() | {"key": "24328"}])
        self.assertTrue(any("a note row has no key" in e for e in errors), errors)

    def test_an_empty_source_on_an_ability_fails(self):
        errors = self.errors([trash_mob(), trash_ability(source=""), trash_note()])
        self.assertTrue(any("empty source" in e for e in errors), errors)

    def test_a_second_mob_row_for_an_entry_fails(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note(), trash_mob(kill="1")])
        self.assertTrue(any("already has a mob row" in e for e in errors), errors)

    def test_a_second_mob_row_on_another_map_fails(self):
        errors = self.errors(
            [trash_mob(), trash_ability(), trash_note(), trash_mob(map="scholomance")],
            raids=[["zulgurub", "309", "Zul'Gurub"], ["scholomance", "289", "Scholomance"]],
        )
        self.assertTrue(any("already has a mob row on map 'zulgurub'" in e for e in errors), errors)

    def test_an_identical_second_mob_row_is_tolerated(self):
        errors = self.errors([trash_mob(), trash_ability(), trash_note(), trash_mob()])
        self.assertEqual([], errors)

    def test_an_identical_second_mob_row_keeps_its_entry_current(self):
        # The tolerated repeat is still that mob's own row: the rows that follow it belong to that entry, not to the
        # one whose block came between.
        errors = self.errors(
            [
                trash_mob(), trash_ability(), trash_note(),
                trash_mob(entry="14884", source="boss.cpp:2"), trash_ability(entry="14884"),
                trash_mob(), trash_note(),
            ]
        )
        self.assertEqual([], errors)

    def test_a_citation_that_names_no_spell_fails(self):
        errors = self.errors([trash_mob(), trash_ability(source="boss.cpp:4"), trash_note()])
        self.assertTrue(any("do not define spell 24328" in e for e in errors), errors)

    def test_a_citation_that_names_no_creature_fails(self):
        errors = self.errors([trash_mob(source="boss.cpp:6"), trash_ability(), trash_note()])
        self.assertTrue(any("do not define creature 14750" in e for e in errors), errors)

    def test_a_note_row_needs_no_citation(self):
        fx = self.make()
        self.assertEqual("", fx.trash_rows[-1]["source"])
        self.assertEqual([], fx.validate()[0])

    def test_a_stale_trash_json_fails(self):
        fx = self.make()
        fx.write_trash([trash_mob(kill="1"), trash_ability(), trash_note()])
        errors = fx.validate()[0]
        self.assertTrue(any("differs from a fresh build" in e and "trash" in e for e in errors), errors)

    def test_a_missing_trash_json_fails(self):
        fx = self.make()
        fx.trash_json.unlink()
        errors = fx.validate()[0]
        self.assertTrue(any("committed JSON not found" in e and "trash" in e for e in errors), errors)

    def test_the_built_document_carries_the_mob_and_its_flags(self):
        fx = self.make()
        doc = json.loads(fx.trash_json.read_text())
        self.assertEqual([{"key": "zulgurub", "mapId": 309}], doc["maps"])
        mob = doc["mobs"]["14750"]
        self.assertEqual(("Gurubashi Bat Rider", "zulgurub", 3, ["aoe"], 3),
                         (mob["name"], mob["map"], mob["danger"], mob["roles"], mob["kill"]))
        ability = mob["abilities"][0]
        self.assertEqual({"sid": 24328, "name": "Corrupted Blood", "int": False, "t": "aoe", "aoe": True,
                          "ground": False, "cone": False, "r": 10, "ok": ""}, ability)
        self.assertEqual(["two wipes at the pull"], mob["notes"])

    def test_the_trash_json_is_minified(self):
        fx = self.make()
        text = fx.trash_json.read_text()
        self.assertNotIn("\n  ", text)
        self.assertEqual(text, boss_data.dump_trash_json(json.loads(text)))


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
        _, bosses = boss_data.make_bosses(rows, names, raids, extra_maps=TRASH_MAPS)
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
        self.doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)

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
            extra_maps=TRASH_MAPS,
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
    "effectImplicitTargetA1", "effectImplicitTargetA2", "effectImplicitTargetA3",
    "effectImplicitTargetB1", "effectImplicitTargetB2", "effectImplicitTargetB3",
    "effectRadiusIndex1", "effectRadiusIndex2", "effectRadiusIndex3",
    "effectTriggerSpell1", "effectTriggerSpell2", "effectTriggerSpell3",
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
# ScriptMgr.h CastFlags: a triggered cast has no cast time (Creature.cpp:3715), so it cannot be interrupted.
CF_TRIGGERED = 0x002

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
                current.append(char)
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    if i + 1 < size and values[i + 1] == quote:
                        current.append(values[i + 1])
                        i += 1
                    else:
                        quote = None
                i += 1
                continue
            if char in "'\"":
                quote = char
                current.append(char)
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


CREATURE_SPELLS_COLUMNS = {"entry", "castFlags_1", "castFlags_2", "castFlags_3", "castFlags_4",
                           "castFlags_5", "castFlags_6", "castFlags_7", "castFlags_8"} | {
    "spellId_%d" % index for index in range(1, 9)}
EVENTAI_EVENT_COLUMNS = {"creature_id", "action1_script", "action2_script", "action3_script"}
EVENTAI_SCRIPT_COLUMNS = {"id", "command", "datalong", "datalong2"}
CREATURE_AI_COLUMNS = {"entry", "ai_name", "script_name", "spell_list_id"}


def _read_creature_ai(entries, core=CORE_DIR):
    """{entry: (ai_name, script_name)} for `entries`, missing names as ''. A `script_name` picks the scripted AI
    first (CreatureAISelector.cpp:45-46), so it overrides both creature_spells and EventAI."""
    template = core / "sql" / "base" / "tw_world_creature_template.sql"
    found = {}
    if template.is_file():
        for row in _read_dump_table(template, "creature_template", CREATURE_AI_COLUMNS):
            entry = row.get("entry")
            if entry in entries:
                found[entry] = (row.get("ai_name") or "", row.get("script_name") or "")
    return found


def _read_trash_casts(entries, core=CORE_DIR):
    """{(creature entry, spell id): cast flags} for `entries`, from the creature_spells list a mob's
    `spell_list_id` names and from its EventAI cast commands (command 15: datalong spell, datalong2 flags).
    Only the AI the core actually runs counts: a `script_name` beats both, EventAI commands count for a mob whose
    `ai_name` is 'EventAI', and the creature_spells list counts only when no `script_name` overrides it."""
    base = core / "sql" / "base"
    casts = {}
    lists = {}
    ai = _read_creature_ai(entries, core=core)
    template = base / "tw_world_creature_template.sql"
    if template.is_file():
        for row in _read_dump_table(template, "creature_template", CREATURE_AI_COLUMNS):
            entry = row.get("entry")
            # A script that calls ScriptedAI::UpdateAI does run this list (ScriptedAI.cpp:82-83); the exclusion
            # holds for the current scripted entries only.
            if entry in entries and row.get("spell_list_id") and not ai.get(entry, ("", ""))[1]:
                lists.setdefault(row["spell_list_id"], set()).add(entry)
    table = base / "tw_world_creature_spells.sql"
    if table.is_file() and lists:
        for row in _read_dump_table(table, "creature_spells", CREATURE_SPELLS_COLUMNS):
            list_id = row.get("entry")
            if list_id not in lists:
                continue
            for index in range(1, 9):
                spell = row.get("spellId_%d" % index)
                if not spell:
                    continue
                flags = row.get("castFlags_%d" % index) or 0
                for entry in lists[list_id]:
                    casts[(entry, spell)] = flags
    events = base / "tw_world_creature_ai_events.sql"
    scripts = base / "tw_world_creature_ai_scripts.sql"
    if events.is_file() and scripts.is_file():
        wanted = set()
        for row in _read_dump_table(events, "creature_ai_events", EVENTAI_EVENT_COLUMNS):
            entry = row.get("creature_id")
            if entry not in entries or ai.get(entry, ("", ""))[0] != "EventAI" or ai[entry][1]:
                continue
            for column in ("action1_script", "action2_script", "action3_script"):
                if row.get(column):
                    wanted.add(row[column])
        by_script = {}
        for row in _read_dump_table(scripts, "creature_ai_scripts", EVENTAI_SCRIPT_COLUMNS):
            if row.get("id") in wanted and row.get("command") == 15:
                by_script.setdefault(row["id"], []).append((row.get("datalong"), row.get("datalong2") or 0))
        for row in _read_dump_table(events, "creature_ai_events", EVENTAI_EVENT_COLUMNS):
            entry = row.get("creature_id")
            if entry not in entries or ai.get(entry, ("", ""))[0] != "EventAI" or ai[entry][1]:
                continue
            for column in ("action1_script", "action2_script", "action3_script"):
                for spell, flags in by_script.get(row.get(column), ()):
                    if spell:
                        casts[(entry, spell)] = flags
    return casts


TEMPLATE_SLOT_COLUMNS = {"entry", "spell_id1", "spell_id2", "spell_id3", "spell_id4"}
CITE_WINDOW = 3


def _read_template_slots(entries, core=CORE_DIR):
    """{entry: {spell ids in its creature_template spell_id1-4 slots}}."""
    template = core / "sql" / "base" / "tw_world_creature_template.sql"
    found = {}
    if template.is_file():
        for row in _read_dump_table(template, "creature_template", TEMPLATE_SLOT_COLUMNS):
            entry = row.get("entry")
            if entry in entries:
                found[entry] = {row.get("spell_id%d" % n) for n in (1, 2, 3, 4)} - {0, None}
    return found


_REGISTRATIONS = {}


def _script_registration_files(script_name, core=CORE_DIR):
    """Core-relative paths of the .cpp files under src/ that register `script_name` (`Name = "<script_name>"`)."""
    key = (str(core), script_name)
    if key not in _REGISTRATIONS:
        out = subprocess.run(
            ["grep", "-rlE", "--include=*.cpp", r'Name\s*=\s*"%s"' % re.escape(script_name), str(core / "src")],
            capture_output=True, text=True, check=False,
        ).stdout.split()
        _REGISTRATIONS[key] = {str(Path(p).relative_to(core)) for p in out}
    return _REGISTRATIONS[key]


def _script_cite_problems(row, script_name, slots=(), core=CORE_DIR):
    """Why the script citation of a scripted entry's ability row does not hold, [] when it does. Every cited
    .cpp/.h part must exist with its lines in bounds; one must be in the file that registers `script_name` and hold
    the row's spell id (or a constant that resolves to it) within CITE_WINDOW lines of the cited line. A generic
    dispatcher script (generic_spell_ai) names no spell: its cast comes from the creature_template spell slot, so
    the slot must hold the spell and the cited line must be where the script reads or casts it."""
    spell = int(row["key"])
    try:
        parts = [c for c in boss_data.parse_source(row) if c[0] == "core" and re.search(r"\.(cpp|h)$", c[1])]
    except boss_data.DataError as error:
        return [str(error)]
    if not parts:
        return ["cites no .cpp/.h script line"]
    registered = _script_registration_files(script_name, core)
    if not registered:
        return ["no src/ file registers script %r" % script_name]
    problems, backed, in_registering_file = [], False, False
    for _, path, start, end in parts:
        file = core / path
        if not file.is_file():
            problems.append("%s does not exist" % path)
            continue
        lines = file.read_text(encoding="utf-8", errors="replace").splitlines()
        if start < 1 or end > len(lines):
            problems.append("%s:%d-%d is out of bounds (%d lines)" % (path, start, end, len(lines)))
            continue
        if path not in registered:
            continue
        in_registering_file = True
        window = list(enumerate(lines, 1))[max(0, start - 1 - CITE_WINDOW):end + CITE_WINDOW]
        if boss_data._has_id(window, spell, boss_data.constant_values(lines)):
            backed = True
        elif spell in slots and any(re.search(r"spells\[|spellId", text) for _, text in window):
            backed = True
    if not in_registering_file:
        problems.append("no cited file registers script %r (it is in %s)" % (script_name, sorted(registered)))
    elif not backed and not problems:
        problems.append("the cited line holds no cast of spell %d (nor a constant for it)" % spell)
    return problems


def _shape_spell_ids(path):
    return [int(match.group(1)) for match in
            (re.match(r"^(\d+),", line.strip()) for line in path.read_text().splitlines())
            if match]


def _spell_radii_yards(spell):
    """The yards of a spell's own radius indexes (SpellRadius.dbc, see TRASH_RADIUS_YARDS)."""
    return {
        TRASH_RADIUS_YARDS[spell["effectRadiusIndex%d" % n]]
        for n in (1, 2, 3)
        if spell.get("effectRadiusIndex%d" % n) in TRASH_RADIUS_YARDS
    }


def _applies_aura(spell):
    return any(
        spell.get("effect%d" % index) in AURA_EFFECTS
        and spell.get("effectApplyAuraName%d" % index)
        for index in (1, 2, 3)
    )


def _kickable(spell, strict=False):
    """Spell::EffectInterruptCast: the prevention type gates both slots, and a
    channel is stopped by its channel flags, not by InterruptFlags. `strict` also
    applies the instant-spell guard (`!spell->GetCastTime()`, castingTimeIndex 1),
    which the boss rows predate."""
    if spell["preventionType"] != SPELL_PREVENTION_TYPE_SILENCE:
        return False
    if spell["attributesEx"] & SPELL_ATTR_EX_CHANNELED:
        return bool(spell["channelInterruptFlags"] & CHANNEL_FLAG_INTERRUPT)
    if strict and spell["castingTimeIndex"] <= 1:
        return False
    return bool(spell["interruptFlags"] & SPELL_INTERRUPT_FLAG_DAMAGE)


def _interrupt_data(spell):
    if spell["attributesEx"] & SPELL_ATTR_EX_CHANNELED:
        return ("channeled (attributesEx 0x%x), preventionType %s, channelInterruptFlags 0x%x"
                % (spell["attributesEx"], spell["preventionType"],
                   spell["channelInterruptFlags"]))
    return ("generic, preventionType %s, interruptFlags 0x%x"
            % (spell["preventionType"], spell["interruptFlags"]))


def _row_where(row):
    owner = row["key"] or row.get("boss") or row.get("entry") or ""
    return "line %s (%s, %s)" % (row["_line"], row["map"], owner)


def source_spell_problems(rows, spells, shape_ids, casts=None, strict=False):
    """Every disagreement between the CSV rows and the core's spell data. `casts` maps (creature entry, spell id)
    to the cast flags its AI uses (`creature_spells.castFlags_*` or an EventAI command 15's `datalong2`), so a row
    marked `int` on a cast the core makes with CF_TRIGGERED (no cast time) is caught; `strict` applies the
    instant-spell guard to the generic branch (the trash rows' rule, the boss rows predate it)."""
    problems = []

    def add(check, spell, where, detail):
        problems.append({"check": check, "spell": spell, "where": where, "detail": detail})

    for row in rows:
        where = _row_where(row)
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
        flags = boss_data.parse_flags(row["d"], allow_radius=True)
        spell = spells.get(spell_id)
        if spell is None:
            add("ability-missing", spell_id, where, "no spell_template row")
            continue
        kickable = _kickable(spell, strict=strict)
        cast_flags = (casts or {}).get((int(row.get("entry") or 0), spell_id), 0)
        if flags["int"] and cast_flags & CF_TRIGGERED:
            add("int-triggered-cast", spell_id, where,
                "the AI casts it with CF_TRIGGERED (flags 0x%x): no cast time to interrupt" % cast_flags)
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
        cls.map_doc, cls.bosses = boss_data.make_bosses(rows, names, extra_maps=TRASH_MAPS)

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
        doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
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
        cls.doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)

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
    # Upper Blackrock Spire (#1389): timers stay 0 (creature_spells stores seconds), flags as UbrsValueTest pins them.
    (9816, 23462): (0, 6000, 6000, "aoe", "", set()),
    (9816, 23341): (0, 14000, 14000, "aoe", "", {"aura", "dispel-nodispel"}),
    (9816, 20228): (0, 15000, 15000, "random", "", {"aura", "dispel-nodispel"}),
    (10429, 13736): (0, 0, 0, "aoe", "", {"int", "interrupt-nokick"}),
    (10429, 15708): (0, 0, 0, "victim", "", {"aura", "dispel-nodispel"}),
    (10429, 15754): (0, 0, 0, "victim", "", set()),
    (10429, 8269): (0, 0, 0, "self", "", {"self", "hp20"}),
    (10339, 16359): (0, 0, 0, "aoe", "", {"aura", "cone"}),
    (10339, 16350): (0, 0, 0, "aoe", "", {"aura", "cone"}),
    (10339, 16390): (0, 0, 0, "aoe", "", {"cone", "int"}),
    (10339, 10101): (0, 0, 0, "victim", "", set()),
    (10430, 16785): (0, 14000, 20000, "aoe", "", set()),
    (10430, 14100): (13000, 16000, 18000, "aoe", "", {"aura", "dispel-nodispel"}),
    (10430, 16636): (0, 15000, 20000, "random", "", set()),
    (10430, 16788): (10000, 10000, 12000, "random", "", {"int", "interrupt-nokick"}),
    (10430, 14144): (0, 14000, 20000, "self", "", {"self"}),
    (10430, 15506): (0, 0, 0, "self", "", {"self"}),
    (10363, 12097): (0, 0, 0, "victim", "", {"aura", "dispel-nodispel"}),
    (10363, 15588): (0, 0, 0, "aoe", "", {"aura"}),
    (10363, 15754): (0, 0, 0, "victim", "", set()),
    (10363, 16419): (0, 0, 0, "random", "", {"ground", "int"}),
    (10363, 16789): (0, 0, 0, "self", "", {"self"}),
    (10363, 16805): (0, 0, 0, "victim", "", {"aura", "dispel-nodispel"}),
    (16042, 27286): (0, 8000, 12000, "victim", "phase1", set()),
    (16042, 27249): (10000, 10000, 10000, "self", "phase1", {"summon=16066"}),
    (16042, 27382): (0, 0, 0, "self", "phase3", {"self"}),
    (16042, 8269): (0, 0, 0, "self", "", {"self", "hp40"}),
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
    9816: [(10316, "ward", 1)],
    10429: [(10339, "linked", 1)],
    16042: [(16066, "summon", 1), (16073, "trigger", 0)],
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
        cls.doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
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


class DungeonTrollValueTest(unittest.TestCase):
    """Pins every Zul'Farrak and Sunken Temple row read off the core sources.

    The fixture below is written from the world dumps and scripts, not generated
    from the CSV: creature_spells seconds are 0 in the columns and EventAI timers
    are ms. The one phase rule is Theka's transform aura, which makes him immune
    to physical and shadow for its duration; the enrage auras stop no ability.
    """

    ABILITIES = {
        (8127, 11314): ("0", "0", "0", "t=self;summon=7844"),
        (8127, 10413): ("0", "0", "0", "t=victim;ok=tank"),
        (8127, 8376): ("0", "0", "0", "t=self;summon=6066"),
        (8127, 11899): ("0", "0", "0", "t=self;summon=8179;int"),
        (8127, 11894): ("0", "0", "0", "t=self;summon=8156;hp75"),
        (8127, 11895): ("0", "0", "0", "t=self;int;hp20"),
        (8127, 10915): ("0", "0", "0", "t=self;int;hp60"),
        (8156, 11020): ("0", "0", "0", "t=victim;aura"),
        (7272, 8600): ("0", "0", "0", "t=random;aura;int"),
        (7272, 11089): ("0", "0", "0", "t=self;self;hp30;ok.purge=nopurge"),
        (7795, 12491): ("0", "0", "0", "t=ally;int"),
        (7795, 11086): ("0", "0", "0", "t=self;summon=7785"),
        (7795, 15245): ("0", "0", "0", "t=aoe;int"),
        (7795, 12739): ("0", "0", "0", "t=victim;int"),
        (7273, 11836): ("0", "0", "0", "t=random;aura;int"),
        (7273, 11902): ("0", "0", "0", "t=aoe;ok=heal"),
        (7273, 11131): ("0", "0", "0", "t=random;aura;int;ok.dispel=nodispel"),
        (7271, 10731): ("0", "18000", "18000", "t=self;ok=script"),
        (7271, 12739): ("0", "0", "0", "t=random;int"),
        (7271, 15245): ("0", "0", "0", "t=aoe;int"),
        (7271, 11086): ("0", "0", "0", "t=self;summon=7785"),
        (7271, 12491): ("0", "0", "0", "t=ally;int"),
        (7604, 11972): ("5000", "15000", "15000", "t=victim;ok=tank"),
        (7604, 12170): ("8000", "10000", "10000", "t=victim;ok=tank"),
        (7608, 9734): ("0", "0", "0", "t=victim"),
        (7608, 11640): ("0", "0", "0", "t=ally;int"),
        (7608, 11642): ("0", "0", "0", "t=ally;int"),
        (7608, 11974): ("0", "0", "0", "t=self;self"),
        (7605, 7159): ("0", "0", "0", "t=victim"),
        (7605, 12540): ("0", "0", "0", "t=victim"),
        (7606, 11990): ("0", "0", "0", "t=aoe;ground;int"),
        (7606, 11962): ("0", "0", "0", "t=victim;aura;int"),
        (7606, 12741): ("0", "0", "0", "t=victim;aura"),
        (7606, 9613): ("0", "0", "0", "t=victim;int"),
        (7267, 7366): ("0", "0", "0", "t=self;self;ok.purge=nopurge"),
        (7267, 15496): ("0", "0", "0", "t=victim;ok=tank"),
        (7267, 11837): ("0", "0", "0", "t=aoe;cone"),
        (7267, 8269): ("0", "0", "0", "t=self;self;hp60;ok.purge=nopurge"),
        (7274, 7366): ("0", "0", "0", "t=self;self;ok.purge=nopurge"),
        (7274, 15496): ("0", "0", "0", "t=victim;ok=tank"),
        (7274, 7160): ("0", "30000", "30000", "t=victim;ok=tank"),
        (7275, 13704): ("0", "0", "0", "t=aoe;aura;ok.dispel=nodispel"),
        (7275, 12039): ("0", "0", "0", "t=ally;int"),
        (7275, 8362): ("0", "0", "0", "t=ally;aura;int"),
        (7275, 15537): ("0", "0", "0", "t=victim;int"),
        (8580, 12887): ("0", "0", "0", "t=aoe;cone"),
        (8580, 6524): ("0", "0", "0", "t=aoe;aura;ok.dispel=nodispel"),
        (5720, 12884): ("0", "0", "0", "t=aoe;cone;aura;ok.dispel=nodispel"),
        (5720, 12882): ("0", "0", "0", "t=aoe;cone"),
        (5721, 12884): ("0", "0", "0", "t=aoe;cone;aura;ok.dispel=nodispel"),
        (5721, 12882): ("0", "0", "0", "t=aoe;cone"),
        (5710, 12468): ("0", "0", "0", "t=aoe;ground;int;aura;ok.dispel=nodispel"),
        (5710, 8376): ("0", "0", "0", "t=self;summon=6066"),
        (5710, 12479): ("0", "20000", "25000", "t=random;aura;ok.dispel=nodispel"),
        (5710, 12480): ("0", "0", "0", "t=random;aura"),
        (5710, 12492): ("0", "0", "0", "t=ally;int"),
        (5711, 12493): ("0", "0", "0", "t=random;aura"),
        (5711, 11639): ("0", "0", "0", "t=random;aura"),
        (5711, 12471): ("0", "0", "0", "t=victim;int"),
        (5719, 12884): ("0", "0", "0", "t=aoe;cone;aura;ok.dispel=nodispel"),
        (5719, 12882): ("0", "0", "0", "t=aoe;cone"),
        (5722, 12884): ("0", "0", "0", "t=aoe;cone;aura;ok.dispel=nodispel"),
        (5722, 12882): ("0", "0", "0", "t=aoe;cone"),
        (5709, 3391): ("0", "0", "0", "t=victim;ok=tank"),
        (5709, 12890): ("0", "0", "0", "t=random;aura;int"),
        (5709, 11876): ("0", "0", "0", "t=aoe;aura;ok.dispel=nodispel"),
    }

    ADDS = {
        8127: [(8138, "summon", 0), (8156, "summon", 1), (7844, "totem", 1),
               (6066, "totem", 1), (8179, "ward", 1)],
        7795: [(7785, "ward", 1), (7786, "summon", 0)],
        7271: [(7286, "summon", 1), (7276, "summon", 1), (7785, "ward", 1),
               (7786, "summon", 0)],
        7604: [(7605, "linked", 3), (7606, "linked", 2), (7608, "linked", 1)],
        7267: [(7797, "linked", 1)],
        7275: [(7796, "linked", 1), (7787, "summon", 0), (7788, "summon", 0),
               (8876, "summon", 0)],
        5710: [(6066, "totem", 1), (5711, "linked", 1), (5263, "linked", 0),
               (5271, "linked", 0), (5273, "linked", 0)],
        5719: [(5722, "linked", 1)],
        5709: [(5277, "linked", 0), (5280, "linked", 0), (8319, "linked", 0),
               (5283, "linked", 0)],
    }

    MAPS = ("zulfarrak", "sunken_temple")

    def rows(self):
        return [r for r in boss_data.read_rows(boss_data.DEFAULT_CSV) if r["map"] in self.MAPS]

    def test_abilities_match_source(self):
        got = {}
        for row in self.rows():
            if row["row"] == "ability":
                got[(int(row["boss"]), int(row["key"]))] = (row["a"], row["b"], row["c"], row["d"])
        self.assertEqual(got, self.ABILITIES)

    def test_adds_and_theka_phase_rule(self):
        adds = {}
        phases = []
        for row in self.rows():
            if row["row"] == "add":
                adds.setdefault(int(row["boss"]), []).append(
                    (int(row["key"]), row["a"], int(row["b"]))
                )
            elif row["row"] == "phase":
                phases.append((int(row["boss"]), row["key"], row["a"], int(row["b"])))
        self.assertEqual(adds, self.ADDS)
        self.assertEqual(phases, [(7272, "transform", "aura=11089", 1)])

    def test_event_timers_and_creature_spell_zeroes(self):
        rows = {
            (int(r["boss"]), int(r["key"])): r
            for r in self.rows() if r["row"] == "ability"
        }
        # EventAI values are ms and stay in the columns.
        self.assertEqual((rows[(7271, 10731)]["b"], rows[(7271, 10731)]["c"]), ("18000", "18000"))
        self.assertEqual((rows[(5710, 12479)]["b"], rows[(5710, 12479)]["c"]), ("20000", "25000"))
        self.assertEqual((rows[(7274, 7160)]["b"], rows[(7274, 7160)]["c"]), ("30000", "30000"))
        # Script timers are literals in the C++ file.
        self.assertEqual((rows[(7604, 11972)]["a"], rows[(7604, 11972)]["b"]), ("5000", "15000"))
        # Deep Slumber is a creature_spells entry: seconds, so no timer in the columns.
        self.assertEqual((rows[(5709, 12890)]["b"], rows[(5709, 12890)]["c"]), ("0", "0"))

    def test_document_carries_only_theka_phase(self):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)
        for boss in doc["bosses"].values():
            if boss["map"] in self.MAPS:
                if boss["entry"] == 7272:
                    self.assertEqual(
                        boss["phases"],
                        [{"name": "transform", "order": 1, "rule": {"kind": "aura", "aura": 11089}}],
                    )
                else:
                    self.assertEqual(boss["phases"], [], boss["name"])
                for ability in boss["abilities"]:
                    self.assertEqual(ability["castMs"], 0, ability["name"])


class UbrsValueTest(unittest.TestCase):
    """The UBRS rows, pinned to values read off the core scripts and tw_world rows.

    The fixture is transcribed from the cited source, not generated from the CSV:
    timers, target, flags, phase and cast ms for every ability, plus every phase
    rule and add. A creature_spells row stores seconds on one SQL line, so Rend's,
    Gyth's and Drakkisath's columns stay 0 and their cadence is in the notes.
    """

    EXPECTED = {
        "9816": {
            "name": "Pyroguard Emberseer",
            "abilities": {
                23462: (0, 6000, 6000, "aoe", "", ()),
                23341: (0, 14000, 14000, "aoe", "", ("aura", "ok.dispel=nodispel")),
                20228: (0, 15000, 15000, "random", "", ("aura", "ok.dispel=nodispel")),
            },
            "phases": [],
            "adds": {10316: ("ward", 1)},
        },
        "10429": {
            "name": "Warchief Rend Blackhand",
            "abilities": {
                13736: (0, 0, 0, "aoe", "", ("int", "ok.interrupt=nokick")),
                15708: (0, 0, 0, "victim", "", ("aura", "ok.dispel=nodispel")),
                15754: (0, 0, 0, "victim", "", ()),
                8269: (0, 0, 0, "self", "", ("self", "hp20")),
            },
            "phases": [],
            "adds": {10339: ("linked", 1)},
        },
        "10339": {
            "name": "Gyth",
            "abilities": {
                16359: (0, 0, 0, "aoe", "", ("aura", "cone")),
                16350: (0, 0, 0, "aoe", "", ("aura", "cone")),
                16390: (0, 0, 0, "aoe", "", ("cone", "int")),
                10101: (0, 0, 0, "victim", "", ()),
            },
            "phases": [],
            "adds": {},
        },
        "10430": {
            "name": "The Beast",
            "abilities": {
                16785: (0, 14000, 20000, "aoe", "", ()),
                14100: (13000, 16000, 18000, "aoe", "", ("aura", "ok.dispel=nodispel")),
                16636: (0, 15000, 20000, "random", "", ()),
                16788: (10000, 10000, 12000, "random", "", ("int", "ok.interrupt=nokick")),
                14144: (0, 14000, 20000, "self", "", ("self",)),
                15506: (0, 0, 0, "self", "", ("self",)),
            },
            "phases": [],
            "adds": {},
        },
        "10363": {
            "name": "General Drakkisath",
            "abilities": {
                12097: (0, 0, 0, "victim", "", ("aura", "ok.dispel=nodispel")),
                15588: (0, 0, 0, "aoe", "", ("aura",)),
                15754: (0, 0, 0, "victim", "", ()),
                16419: (0, 0, 0, "random", "", ("ground", "int")),
                16789: (0, 0, 0, "self", "", ("self",)),
                16805: (0, 0, 0, "victim", "", ("aura", "ok.dispel=nodispel")),
            },
            "phases": [],
            "adds": {},
        },
        "16042": {
            "name": "Lord Valthalak",
            "abilities": {
                27286: (0, 8000, 12000, "victim", "phase1", ()),
                27249: (10000, 10000, 10000, "self", "phase1", ("summon=16066",)),
                27382: (0, 0, 0, "self", "phase3", ("self",)),
                8269: (0, 0, 0, "self", "", ("self", "hp40")),
            },
            "phases": [
                ("phase3", "hp", 15, 1),
                ("phase2", "hp", 40, 2),
                ("phase1", "start", None, 3),
            ],
            "adds": {16066: ("summon", 1), 16073: ("trigger", 0)},
        },
    }

    @classmethod
    def setUpClass(cls):
        rows = boss_data.read_rows(boss_data.DEFAULT_CSV)
        names = boss_data.read_names(boss_data.DEFAULT_NAMES)
        cls.doc = boss_data.build_document(rows, names, extra_maps=TRASH_MAPS)

    def boss(self, entry):
        return self.doc["bosses"][entry]

    @staticmethod
    def flag_set(ability):
        flags = {
            name
            for name in ("aura", "self", "mark", "ground", "cone", "tail", "int")
            if ability[name]
        }
        if ability["hp"]:
            flags.add(f"hp{ability['hp']}")
        if ability["summon"]:
            flags.add(f"summon={ability['summon']}")
        if ability["ok"]:
            flags.add(f"ok={ability['ok']}")
        for need, reason in ability["okNeeds"].items():
            flags.add(f"ok.{need}={reason}")
        return flags

    def test_abilities_match_the_source(self):
        for entry, expected in self.EXPECTED.items():
            boss = self.boss(entry)
            self.assertEqual(boss["name"], expected["name"])
            self.assertEqual(boss["map"], "upper_blackrock_spire")
            got = {
                a["sid"]: (a["a"], a["b"], a["c"], a["target"], a["phase"], self.flag_set(a))
                for a in boss["abilities"]
            }
            want = {
                sid: (a, b, c, target, phase, set(flags))
                for sid, (a, b, c, target, phase, flags) in expected["abilities"].items()
            }
            self.assertEqual(got, want, entry)
            self.assertTrue(all(a["castMs"] == 0 for a in boss["abilities"]), entry)

    def test_phases_and_adds_match_the_source(self):
        for entry, expected in self.EXPECTED.items():
            boss = self.boss(entry)
            got = [
                (p["name"], p["rule"]["kind"], p["rule"].get("hp"), p["order"])
                for p in boss["phases"]
            ]
            self.assertEqual(got, expected["phases"], entry)
            got_adds = {a["entry"]: (a["role"], a["priority"]) for a in boss["adds"]}
            self.assertEqual(got_adds, expected["adds"], entry)

    def test_choices_that_the_panel_depends_on(self):
        val = self.boss("16042")
        self.assertEqual([p["name"] for p in val["phases"]], ["phase3", "phase2", "phase1"])
        self.assertEqual(val["phases"][0]["rule"], {"kind": "hp", "hp": 15})
        volley = next(a for a in val["abilities"] if a["sid"] == 27382)
        self.assertEqual(volley["phase"], "phase3")
        self.assertEqual(volley["target"], "self")
        # 1604205 has inverse phase mask 13, so Shadow Wrath runs in phase 1 only.
        wrath = next(a for a in val["abilities"] if a["sid"] == 27286)
        self.assertEqual(wrath["phase"], "phase1")
        # The Beast's script-pointed 14144 is a self aura in spell_template, not a hit on the victim.
        beast = next(a for a in self.boss("10430")["abilities"] if a["sid"] == 14144)
        self.assertEqual(beast["target"], "self")
        self.assertTrue(beast["self"])
        # Fireball has a cast time but interruptFlags 0: the core cannot kick it.
        fireball = next(a for a in self.boss("10430")["abilities"] if a["sid"] == 16788)
        self.assertEqual(fireball["okNeeds"], {"interrupt": "nokick"})
        # Whirlwind is a channel with preventionType 2: the core cannot kick it either.
        whirlwind = next(a for a in self.boss("10429")["abilities"] if a["sid"] == 13736)
        self.assertEqual(whirlwind["okNeeds"], {"interrupt": "nokick"})
        # Emberseer's timers start at liberation, so no ability has a pull timer.
        emberseer = self.boss("9816")
        self.assertTrue(all(a["a"] == 0 for a in emberseer["abilities"]))
        emberseer_notes = " ".join(emberseer["notes"])
        self.assertIn("6 s after he is freed", emberseer_notes)
        self.assertIn("Flame Buffet 3 s", emberseer_notes)
        self.assertIn("Pyro Blast 14 s", emberseer_notes)
        # Gyth's own block has no verifiable ms timers: the row stores seconds,
        # so the columns are 0 and the cadence is in the note.
        gyth = {a["sid"]: a for a in self.boss("10339")["abilities"]}
        self.assertTrue(all((a["a"], a["b"], a["c"]) == (0, 0, 0) for a in gyth.values()))
        self.assertTrue(gyth[16390]["int"] and gyth[16390]["cone"])
        gyth_notes = " ".join(self.boss("10339")["notes"])
        self.assertIn("first 8 s then every 7 s", gyth_notes)
        self.assertIn("first 11 s then every 16 s", gyth_notes)
        self.assertIn("first 4 s then every 10 s", gyth_notes)
        self.assertIn("first and every 23 s", gyth_notes)
        self.assertIn("Nefarius", gyth_notes)
        # The creature_spells cadences are in the notes because the columns are 0.
        rend_notes = " ".join(self.boss("10429")["notes"])
        self.assertIn("20/18", rend_notes)
        self.assertIn("9/16", rend_notes)
        drak_notes = " ".join(self.boss("10363")["notes"])
        self.assertIn("8-40", drak_notes)
        self.assertIn("10-30", drak_notes)


# ---- the trash table against the core --------------------------------------------------------------------------

# SpellRadius.dbc, index -> yards: 8 -> 5, 10 -> 30, 13 -> 10, 18 -> 15. The index is in spell_template, the yards in
# the client's DBC, so the mapping is pinned here and the test reads the index from the core row.
TRASH_RADIUS_YARDS = {8: 5, 10: 30, 13: 10, 18: 15}

# The effect-1 target pairs that put a spell's area on the caster itself (SpellDefines.h): the caster's source or
# destination location with an enemy or friendly area at it. A spell like that reaches its radius in yards around the
# caster, so its row must carry r=. 46/16 (TARGET_LOCATION_SCRIPT_NEAR_CASTER with the area at the destination) is a
# script target's position near the caster, not the caster's own, so it is no caster-centred area.
CASTER_CENTRED_TARGETS = {(22, 15), (22, 30), (18, 16)}

# The trash rows' values, read from the core lines named in the comments (sql/base of the core checkout), not
# generated from the CSV: (interruptible, target, aoe, ground, cone, radius in yards). A changed CSV field fails.
TRASH_ABILITY_VALUES = {
    # spell_template 19245/19246/19254, creature_spells:127, creature_ai_scripts:58
    (15111, 24611): (True, "victim", False, False, False, 0),
    (15111, 24612): (True, "aoe", True, True, False, 0),
    (15111, 24621): (False, "", False, False, False, 0),
    # 19247/19248/19249, creature_ai_scripts:58: 24614 channels and applies 24615 on itself, whose pulse is the 5 yd
    # area (radius index 8) around the Voidwalker
    (15146, 24614): (True, "self", False, False, False, 5),
    (15146, 24616): (False, "aoe", True, False, False, 15),
    # 18691, GenericSpellAI.cpp:417 reads the creature_template slot and :356 casts it
    (11368, 24023): (False, "victim", False, False, False, 0),
    # 4287/2741/18273/12435/18692, boss_jeklik.cpp:531/547/509/539/526
    (14750, 5115): (False, "ally", False, False, False, 10),
    (14750, 3391): (False, "victim", False, False, False, 0),
    (14750, 23511): (False, "aoe", True, False, False, 10),
    (14750, 16128): (False, "victim", False, False, False, 0),
    (14750, 24024): (False, "aoe", True, False, False, 10),
    # 13363/9982
    (10470, 17165): (True, "victim", False, False, False, 0),
    (10470, 12739): (True, "victim", False, False, False, 0),
    # 12823/13735/13736/9073
    (10471, 16592): (False, "self", False, False, False, 0),
    (10471, 17613): (True, "ally", False, False, False, 0),
    (10471, 17615): (False, "victim", False, False, False, 0),
    (10471, 17165): (True, "victim", False, False, False, 0),
    (10471, 11443): (True, "victim", False, False, False, 0),
    # 13751/13752
    (10499, 17630): (False, "victim", False, False, False, 0),
    (10499, 17631): (False, "aoe", True, False, False, 30),
    # 11666/11942/11750
    (10469, 15043): (True, "victim", False, False, False, 0),
    (10469, 15499): (False, "victim", False, False, False, 0),
    (10469, 15244): (False, "victim", False, False, True, 0),
    # 11525/13737/9486
    (10477, 14887): (True, "aoe", True, False, False, 30),
    (10477, 17616): (False, "aoe", True, False, False, 0),
    (10477, 12020): (False, "aoe", True, False, False, 5),
    # 13349/13431
    (10476, 17151): (False, "ally", False, False, False, 0),
    (10476, 12739): (True, "victim", False, False, False, 0),
    (10476, 17234): (False, "victim", False, False, False, 0),
}

# The mob rows, from creature_template.sql:118 and the run notes quoted in the CSV's own note rows.
TRASH_MOB_VALUES = {
    15111: ("Mad Servant", "zulgurub", 3, ["caster", "summoner"], 3),
    15146: ("Mad Voidwalker", "zulgurub", 3, ["aoe"], 2),
    11368: ("Bloodseeker Bat", "zulgurub", 2, ["patrol"], 1),
    14750: ("Gurubashi Bat Rider", "zulgurub", 3, ["aoe"], 3),
    10470: ("Scholomance Neophyte", "scholomance", 3, ["caster"], 2),
    10471: ("Scholomance Acolyte", "scholomance", 3, ["healer", "caster"], 3),
    10499: ("Spectral Researcher", "scholomance", 3, ["caster", "aoe"], 2),
    10469: ("Scholomance Adept", "scholomance", 3, ["caster", "aoe", "runner"], 2),
    10477: ("Scholomance Necromancer", "scholomance", 3, ["caster", "runner"], 3),
    10476: ("Scholomance Necrolyte", "scholomance", 2, ["caster"], 1),
}


@unittest.skipUnless(
    CORE_SPELL_SQL.is_file(),
    "set TORTOISE_CORE_DIR=<tortoise-wow checkout>: no %s" % CORE_SPELL_SQL,
)
class TrashValueTest(unittest.TestCase):
    """Every trash row's values, read off the cited core lines and not generated from the CSV."""

    @classmethod
    def setUpClass(cls):
        cls.rows = boss_data.read_trash_rows(ROOT / "data" / "boss" / "trash_abilities.csv")
        cls.names = boss_data.read_names(ROOT / "data" / "boss" / "names.json")
        cls.raids = boss_data.read_raids(ROOT / "data" / "boss" / "raids.csv")
        cls.spells = _read_spells()
        cls.boss_entries, cls.add_entries = boss_data.boss_entry_sets(
            boss_data.read_rows(ROOT / "data" / "boss" / "boss_abilities.csv")
        )
        cls.maps, cls.mobs = boss_data.make_trash(
            cls.rows, cls.names, cls.raids, cls.boss_entries
        )

    def ability(self, entry, spell):
        return next(a for a in self.mobs[entry]["abilities"] if a["sid"] == spell)

    def test_the_fixture_covers_every_row(self):
        rows = {(int(r["entry"]), int(r["key"])) for r in self.rows if r["row"] == "ability"}
        self.assertEqual(set(TRASH_ABILITY_VALUES), rows)

    def test_mob_values(self):
        self.assertEqual(set(TRASH_MOB_VALUES), set(self.mobs))
        for entry, want in sorted(TRASH_MOB_VALUES.items()):
            mob = self.mobs[entry]
            self.assertEqual(
                want, (mob["name"], mob["map"], mob["danger"], mob["roles"], mob["kill"]), entry
            )

    def test_ability_values(self):
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            ability = self.ability(entry, spell)
            got = (ability["int"], ability["t"], ability["aoe"], ability["ground"], ability["cone"],
                   ability["r"])
            self.assertEqual(want, got, "%s %s" % (entry, spell))

    def test_an_int_row_is_kickable_by_the_core(self):
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            if want[0]:
                self.assertTrue(_kickable(self.spells[spell], strict=True), "%s %s" % (entry, spell))

    def test_a_cone_row_has_a_cone_target(self):
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            if not want[4]:
                continue
            row = self.spells[spell]
            targets = {row.get("effectImplicitTargetA%d" % n, 0) for n in (1, 2, 3)}
            targets |= {row.get("effectImplicitTargetB%d" % n, 0) for n in (1, 2, 3)}
            self.assertIn(24, targets, "%s %s" % (entry, spell))

    def test_a_ground_row_has_a_persistent_area_effect(self):
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            if not want[3]:
                continue
            row = self.spells[spell]
            self.assertIn(27, (row.get("effect1"), row.get("effect2"), row.get("effect3")),
                          "%s %s" % (entry, spell))

    def test_a_radius_row_matches_the_spell_row(self):
        # The radius is the spell's own, or the one of the spell its aura triggers: Consuming Shadows (24614) channels
        # the 5 yd pulse (24615) around the Voidwalker.
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            if not want[5]:
                continue
            row = self.spells[spell]
            yards = _spell_radii_yards(row)
            for n in (1, 2, 3):
                triggered = row.get("effectTriggerSpell%d" % n)
                if triggered in self.spells:
                    yards |= _spell_radii_yards(self.spells[triggered])
            self.assertIn(want[5], yards, "%s %s r=%s, spell radii %s" % (entry, spell, want[5], yards))

    def test_a_caster_centred_area_carries_a_radius(self):
        found = set()
        for row in self.rows:
            if row["row"] != "ability":
                continue
            spell = self.spells[int(row["key"])]
            pair = (spell.get("effectImplicitTargetA1"), spell.get("effectImplicitTargetB1"))
            if pair not in CASTER_CENTRED_TARGETS:
                continue
            key = (int(row["entry"]), int(row["key"]))
            found.add(key)
            self.assertTrue(
                self.ability(*key)["r"], "%s %s is a caster-centred area with no r=" % key
            )
        self.assertEqual(
            {(15146, 24616), (14750, 5115), (14750, 23511), (14750, 24024), (10499, 17631),
             (10477, 14887), (10477, 12020)},
            found,
        )

    def test_a_self_row_applies_an_aura(self):
        for (entry, spell), want in sorted(TRASH_ABILITY_VALUES.items()):
            if want[1] != "self":
                continue
            self.assertTrue(_applies_aura(self.spells[spell]), "%s %s" % (entry, spell))

    def test_no_trash_entry_is_a_boss(self):
        self.assertEqual(set(), set(self.mobs) & self.boss_entries)
        # 11368 is Jeklik's summon add: the trash block is the one place its own casts live.
        self.assertIn(11368, self.add_entries)

    def test_the_seed_set_is_the_brief_s_ten(self):
        self.assertEqual({15111, 15146, 11368, 14750, 10470, 10471, 10499, 10469, 10477, 10476},
                         set(self.mobs))
        self.assertEqual([{"key": "zulgurub", "mapId": 309}, {"key": "scholomance", "mapId": 289}],
                         self.maps)

    def test_the_committed_json_matches_the_csv(self):
        doc = boss_data.build_trash_document(
            self.rows, self.names, self.raids, self.boss_entries
        )
        committed = (ROOT / "tools" / "observability" / "web" / "data" / "trash_abilities.json")
        self.assertEqual(boss_data.dump_trash_json(doc), committed.read_text(encoding="utf-8"))


# Rows that deliberately stay without `int` although the core can kick them and the AI casts them untriggered: the
# value is the reason. There is none today; an entry here is a decision, not an oversight, and it fails the test once
# the spell stops being kickable, so the list cannot rot.
TRASH_INT_OMISSIONS = {}


@unittest.skipUnless(
    CORE_SPELL_SQL.is_file(),
    "set TORTOISE_CORE_DIR=<tortoise-wow checkout>: no %s" % CORE_SPELL_SQL,
)
class TrashSpellSourceTest(unittest.TestCase):
    """The trash rows against the core's spell data, through the same helper as the boss rows: the kick rule with
    the instant-spell guard and CF_TRIGGERED, and the self/aura rule."""

    @classmethod
    def setUpClass(cls):
        cls.spells = _read_spells()
        cls.rows = boss_data.read_trash_rows(ROOT / "data" / "boss" / "trash_abilities.csv")
        cls.casts = _read_trash_casts({int(r["entry"]) for r in cls.rows})
        cls.ai = _read_creature_ai({int(r["entry"]) for r in cls.rows})
        cls.slots = _read_template_slots({int(r["entry"]) for r in cls.rows})

    def source_problems(self, rows=None, casts=None):
        return source_spell_problems(
            self.rows if rows is None else rows,
            self.spells,
            [],
            casts=self.casts if casts is None else casts,
            strict=True,
        )

    def test_the_core_source_backs_every_row(self):
        self.assertEqual([], self.source_problems())

    def test_the_cast_flags_are_read_from_the_core(self):
        # 24621 Portal of Madness is the EventAI cast with CF_TRIGGERED (creature_ai_scripts:58, datalong2 2).
        self.assertTrue(self.casts[(15111, 24621)] & CF_TRIGGERED)
        # 12739 Shadow Bolt comes from the creature_spells list with castFlags 8 (CF_MAIN_RANGED_SPELL).
        self.assertFalse(self.casts[(10470, 12739)] & CF_TRIGGERED)
        self.assertTrue(self.casts[(10470, 12739)] & 0x008)

    def test_a_scripted_entry_cites_its_script(self):
        """A creature a `script_name` gives an AI runs neither its creature_spells list nor its EventAI
        (CreatureAISelector.cpp:45-46), so its casts live in the script: every ability row of such an entry must
        cite the script line that makes it (the file exists, the line is in bounds, the line or one within three
        holds the spell id or a constant for it, and the file registers the script), not the world table the
        script overrides."""
        scripted = {entry for entry, (_, script) in self.ai.items() if script}
        self.assertTrue(scripted, "no trash entry has a script_name: check the core read")
        problems = [
            "line %s (%s): %s ability %s, script %r: %s (source %r)"
            % (row["_line"], row["map"], row["entry"], row["key"], self.ai[int(row["entry"])][1], why, row["source"])
            for row in self.rows
            if row["row"] == "ability" and int(row["entry"]) in scripted
            for why in _script_cite_problems(
                row, self.ai[int(row["entry"])][1], self.slots.get(int(row["entry"]), ()))
        ]
        self.assertEqual([], problems)

    def test_a_script_citation_that_does_not_hold_is_caught(self):
        row = next(r for r in self.rows if r["row"] == "ability" and int(r["entry"]) == 14750
                   and int(r["key"]) == 23511)
        script = self.ai[14750][1]
        self.assertEqual([], _script_cite_problems(row, script), "the real citation must pass")

        def cited(source):
            return _script_cite_problems(dict(row, source=source), script)

        table = "sql/base/tw_world_spell_template.sql:18273"
        jeklik = "src/scripts/dungeons/zulgurub/boss_jeklik.cpp"
        # 23968 is not cast at boss_jeklik.cpp:509 (the line holds 23511)
        wrong_spell = _script_cite_problems(
            dict(row, key="23968", source="sql/base/tw_world_spell_template.sql:18651;%s:509" % jeklik), script)
        self.assertTrue(wrong_spell and "holds no cast of spell 23968" in wrong_spell[0], wrong_spell)
        # an unrelated .cpp that exists but does not register the script
        unrelated = cited("%s;src/game/AI/GenericSpellAI.cpp:154" % table)
        self.assertTrue(unrelated and "registers script" in unrelated[0], unrelated)
        # a line past the end of the file
        self.assertTrue(any("out of bounds" in p for p in cited("%s;%s:99999" % (table, jeklik))))
        # a file that is not there
        self.assertTrue(any("does not exist" in p for p in cited("%s;src/scripts/no_such_script.cpp:5" % table)))
        # only the world table: no script line at all
        self.assertTrue(cited(table))

    def test_an_ability_row_is_a_cast_the_running_ai_makes(self):
        """Every ability row's spell must be a cast the AI the core runs makes: a row whose spell only lives in a
        table that never runs (a creature_template slot, or an EventAI/creature_spells list a script overrides) is a
        source taken from the wrong place. A scripted entry's cast is in the script, so its row passes by citing the
        script line (test_a_scripted_entry_cites_its_script)."""
        scripted = {entry for entry, (_, script) in self.ai.items() if script}
        problems = []
        for row in self.rows:
            if row["row"] != "ability":
                continue
            entry, spell = int(row["entry"]), int(row["key"])
            if (entry, spell) in self.casts:
                continue
            if entry in scripted and any(re.search(r"\.(cpp|h):\d+", part)
                                         for part in row["source"].split(";")):
                continue
            problems.append(
                "line %s (%s): %s ability %s is no cast the running AI makes (running casts: %s)%s"
                % (row["_line"], row["map"], entry, spell,
                   sorted(s for (e, s) in self.casts if e == entry),
                   "" if entry not in scripted else "; its script cites no script line")
            )
        self.assertEqual([], problems)

    def test_a_triggered_cast_is_no_interrupt(self):
        rows = [dict(r) for r in self.rows]
        for row in rows:
            if row["row"] == "ability" and int(row["key"]) == 24621:
                row["d"] = "summon=15141;int"
        problems = self.source_problems(rows)
        self.assertIn(("int-triggered-cast", 24621), {(p["check"], p["spell"]) for p in problems})

    def test_an_instant_generic_cast_is_no_interrupt(self):
        # 24611 Fireball is kickable as it stands; the instant-spell guard is what the boss rule lacks.
        spell = dict(self.spells[24611])
        self.assertTrue(_kickable(spell, strict=True))
        spell["castingTimeIndex"] = 1
        self.assertFalse(_kickable(spell, strict=True))
        self.assertTrue(_kickable(spell), "the boss rule still calls it kickable")
        row = {"_line": 1, "map": "zulgurub", "entry": "15111", "row": "ability", "key": "24611",
               "d": "t=victim;int"}
        problems = source_spell_problems([row], {24611: spell}, [], strict=True)
        self.assertIn(("int-not-kickable", 24611), {(p["check"], p["spell"]) for p in problems})

    def test_a_broken_row_is_caught(self):
        rows = [dict(r) for r in self.rows]
        for row in rows:
            if row["row"] == "ability" and int(row["key"]) == 24024:
                row["d"] = "t=aoe;int"  # preventionType 0: the core cannot kick it
        problems = self.source_problems(rows)
        self.assertIn(("int-not-kickable", 24024), {(p["check"], p["spell"]) for p in problems})

    def test_a_kickable_cast_carries_int(self):
        """The reverse of the kick rule: a spell the core can stop, cast without CF_TRIGGERED, is an interrupt the page
        must label KICK, so its row carries `int`."""
        for row in self.rows:
            if row["row"] != "ability":
                continue
            entry, spell = int(row["entry"]), int(row["key"])
            if (entry, spell) in TRASH_INT_OMISSIONS:
                continue
            if not _kickable(self.spells[spell], strict=True):
                continue
            if self.casts.get((entry, spell), 0) & CF_TRIGGERED:
                continue
            self.assertTrue(
                boss_data.parse_flags(row["d"], allow_radius=True)["int"],
                "%s %s is kickable and cast untriggered, so the row needs int" % (entry, spell),
            )

    def test_the_int_omission_list_still_reproduces(self):
        for (entry, spell), reason in TRASH_INT_OMISSIONS.items():
            self.assertTrue(reason, "%s %s is listed with no reason" % (entry, spell))
            self.assertTrue(_kickable(self.spells[spell], strict=True), "%s %s" % (entry, spell))
            self.assertFalse(
                self.casts.get((entry, spell), 0) & CF_TRIGGERED,
                "%s %s is cast with CF_TRIGGERED, so it is no deliberate omission" % (entry, spell),
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
