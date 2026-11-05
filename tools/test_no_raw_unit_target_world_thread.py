"""World-thread code never reads a spell's raw unit-target pointer.

SpellCastTargets::m_unitTarget (getUnitTarget) is refreshed only in the caster's next Spell::update on the map thread,
and Map::Remove frees a dead creature (a Brain Wash Totem) at the end of the map tick. The commander (commands/) and
the run leader (runtime/) run on the World thread between map ticks, so a read there can hit a freed unit and crash
the worldserver. They resolve the cast's target with getUnitTargetGuid() and a map lookup instead.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NoRawUnitTargetOnTheWorldThreadTest(unittest.TestCase):
    def test_commands_and_runtime_never_read_get_unit_target(self):
        offenders = []
        for folder in ("commands", "runtime"):
            for path in sorted((ROOT / folder).rglob("*")):
                if path.suffix not in (".cpp", ".h"):
                    continue
                for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                    if "m_targets.getUnitTarget()" in line:
                        offenders.append(f"{path.relative_to(ROOT)}:{n}")
        self.assertEqual([], offenders)


if __name__ == "__main__":
    unittest.main()
