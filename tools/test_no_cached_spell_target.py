"""Playerbot code resolves a spell's target from its guid, never from the cached pointer.

SpellCastTargets::m_unitTarget / m_GOTarget (getUnitTarget / getGOTarget) are refreshed only in the caster's next
Spell::update, and the map frees a unit or game object removed at the end of a map tick. The bot AI runs later in that
same World tick, so a trigger, value or action that reads another unit's - or its own current spell's - cached target
can dereference freed memory and crash the worldserver. CRASH-1714 was this use-after-free reached from a diagnostic
row; this pin keeps the other call sites out. Resolve getUnitTargetGuid() / getGOTargetGuid() with
ObjectAccessor::GetUnit (or a map lookup) instead.

A read is allowed only when its stripped source line is listed in ALLOWED and the source carries a
`cached-target-allow:` comment on that line or in the three lines above it.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = ("ai",)
READS = ("m_targets.getUnitTarget()", "m_targets.getGOTarget()")
MARKER = "cached-target-allow:"
MARKER_LINES_ABOVE = 3

# Justified reads: "<path>:<stripped line>". A new entry needs the marker comment and its reason in the source.
ALLOWED = {
    "ai/playerbot/strategy/actions/UseItemAction.cpp": [
        "GameObject* const go = m_targets.getGOTarget();",
    ],
}


def source_files():
    for folder in SOURCES:
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix in (".cpp", ".h"):
                yield path


def read_lines(path):
    """(stripped code line, index within the file) for every line that reads a cached spell target."""
    lines = path.read_text(errors="replace").splitlines()
    for index, line in enumerate(lines):
        code = line.split("//", 1)[0]
        if any(read in code for read in READS):
            yield code.strip(), index, lines


class NoCachedSpellTargetTest(unittest.TestCase):
    def test_no_cached_spell_target_reads(self):
        offenders = []
        for path in source_files():
            rel = path.relative_to(ROOT).as_posix()
            allowed = ALLOWED.get(rel, [])
            for code, index, lines in read_lines(path):
                if code in allowed and any(MARKER in line for line in lines[max(0, index - MARKER_LINES_ABOVE):index + 1]):
                    continue
                offenders.append(f"{rel}:{index + 1}: {code}")
        self.assertEqual([], offenders)

    def test_allowlisted_reads_keep_their_reason(self):
        for rel, entries in ALLOWED.items():
            path = ROOT / rel
            self.assertTrue(path.is_file(), f"allowlisted file is gone: {rel}")
            lines = path.read_text(errors="replace").splitlines()
            present = [code for code, index, _ in read_lines(path)
                       if any(MARKER in line for line in lines[max(0, index - MARKER_LINES_ABOVE):index + 1])]
            for entry in entries:
                self.assertIn(entry, present, f"{rel}: allowlisted read lost its reason marker: {entry}")


if __name__ == "__main__":
    unittest.main()
