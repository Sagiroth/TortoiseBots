"""The dashboard's breath window is standalone (runtime/BossWindowSignal.h).

A breath the core casts triggered never reaches a cast slot, so the commander
report reads it from the committed rows of runtime/BossWindowData.h instead:
- FillMobFacingAndCast forgets the window when the boss casts from a slot, and
  with no slot spell calls FillBreathWindow;
- FillBreathWindow reads the policy (OpenPoint) and the countdown keyed by map,
  instance and guid, and fills the fields the dashboard draws (cast, castId,
  castLeftMs, castTotalMs, shape);
- both reports that fill a boss's cast prune the countdown;
- nothing of the stand sets is read: no ActiveStandWindowSpell, no
  StandWindowMarks, no ai_boss_stand, and no bot code includes the new headers.
The rows are checked against the core script when ~/tortoise-wow (or
TORTOISE_CORE) is there.
"""
import os
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()
SIGNAL = (ROOT / "runtime" / "BossWindowSignal.h").read_text()
DATA = (ROOT / "runtime" / "BossWindowData.h").read_text()
STAND_NAMES = ("ActiveStandWindowSpell", "StandWindowMarks", "StandWindow::", "s_standWindow",
               "ai_boss_stand", "BossStandPolicy", "BossTacticsStand", "StoppedStandWindowSpell")


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def committed_rows():
    rows = {}
    pattern = re.compile(r"\{249, 10184, 17131, (\d), (-?[\d.]+)f, (-?[\d.]+)f, [\d.]+f, (\d+), (\d), 5000\}")
    for m in pattern.finditer(DATA):
        rows[int(m.group(1))] = (float(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5)))
    return rows


class BossWindowWiringTest(unittest.TestCase):
    def test_report_calls_the_breath_window(self):
        fill = body(COMMANDS, "static void FillMobFacingAndCast(Unit* unit, CommanderTelemetry::MobInfo& mob)")
        loop = fill.index("for (CurrentSpellTypes type")
        self.assertIn("s_breathWindows.Forget(windowKey);", fill[loop:])
        self.assertIn("unit->GetMapId(), unit->GetInstanceId(),", fill)
        self.assertIn("unit->GetObjectGuid().GetRawValue()", fill)
        call = fill.index("FillBreathWindow(unit, windowKey, mob);")
        self.assertGreater(call, fill.index("        return;\n    }\n", loop), "after the slot loop")

    def test_breath_window_fills_the_dashboard_fields(self):
        fn = body(COMMANDS, "static void FillBreathWindow(")
        for read in ("unit->GetEntry()", "unit->GetMapId()", "unit->IsStopped()", "unit->GetTargetGuid().IsEmpty()",
                     "unit->HasAura(aura)", "BossWindow::OpenPoint(facts,"):
            self.assertIn(read, fn)
        self.assertIn("s_breathWindows.Forget(key);", fn)
        self.assertIn("s_breathWindows.Left(key, WorldTimer::getMSTime(), point->windowMs, leftMs)", fn)
        for field in ("mob.cast = SpellNameOf(point->spell);", "mob.castId = point->spell;",
                      "mob.castLeftMs = leftMs;", "mob.castTotalMs = point->windowMs;", "mob.castTarget = 0;",
                      "mob.channel = false;", "SpellShape::For(mob.castId)", "mob.shape = *shape;"):
            self.assertIn(field, fn)

    def test_both_reports_prune(self):
        for start in ("void CollectCommanderTelemetry(std::vector<CommanderTelemetry::BindingRef>& out)",
                      "void CollectDungeonMobs(std::vector<DungeonMobTelemetry::RunRef>& runs)"):
            self.assertIn("s_breathWindows.Prune(WorldTimer::getMSTime());", body(COMMANDS, start))

    def test_no_stand_set_reads(self):
        self.assertIn('#include "../runtime/BossWindowSignal.h"', COMMANDS)
        for text, where in ((COMMANDS, "Commander.cpp"), (SIGNAL, "BossWindowSignal.h"), (DATA, "BossWindowData.h")):
            for name in STAND_NAMES:
                self.assertNotIn(name, text, f"{where} references {name}")
        self.assertNotIn("#include \"../ai/", SIGNAL + DATA)

    def test_no_bot_code_reads_the_signal(self):
        for path in list((ROOT / "ai").rglob("*.h")) + list((ROOT / "ai").rglob("*.cpp")):
            text = path.read_text(errors="replace")
            self.assertNotIn("BossWindowSignal.h", text, path)
            self.assertNotIn("BossWindowData.h", text, path)

    def test_rows_parse(self):
        self.assertEqual(sorted(committed_rows()), list(range(8)))

    def test_rows_match_the_core_script(self):
        core = pathlib.Path(os.environ.get("TORTOISE_CORE", pathlib.Path.home() / "tortoise-wow"))
        script = core / "src" / "scripts" / "dungeons" / "onyxias_lair" / "boss_onyxia.cpp"
        if not script.is_file():
            self.skipTest(f"no core script at {script}")
        text = script.read_text()
        spells = dict(re.findall(r"(SPELL_BREATH_\w+)\s*=\s*(\d+)", text))
        self.assertIn("SPELL_HOVER = 17131", text)
        self.assertIn("m_uiDeepBreathTimer = 5000", text)
        moves = re.findall(r"\{(\d), (\d), (SPELL_BREATH_\w+),\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,", text)
        self.assertEqual(len(moves), 8)
        rows = committed_rows()
        for point, end, spell, x, y in moves:
            self.assertEqual(rows[int(point)], (float(x), float(y), int(spells[spell]), int(end)), point)


if __name__ == "__main__":
    unittest.main()
