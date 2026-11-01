"""The test toolkit reads a spell's target only for a channel.

Spell:GetTarget() returns the core's cached unit pointer, which is refreshed
only while a spell is casting. A projectile in flight keeps a stale pointer
when its target is deleted, and reading it crashed mangosd (ZG walk 16).
"""
import re
import unittest
from pathlib import Path

LIB = Path(__file__).resolve().parent / "eluna/test-toolkit/lua_scripts/tt/tt_lib.ext"


class SpellInfoTargetTest(unittest.TestCase):
    def body(self):
        src = LIB.read_text()
        m = re.search(r"local function spellInfo\(unit, slot\)\n(.*?)\nend\n", src, re.S)
        self.assertIsNotNone(m, "spellInfo not found")
        return m.group(1)

    def test_target_is_read_only_for_the_channel_slot(self):
        body = self.body()
        guard = "if slot ~= TT.SPELL_CHANNELED then return id, nil end"
        self.assertIn(guard, body)
        self.assertLess(body.index(guard), body.index("sp:GetTarget()"))

    def test_no_other_script_reads_a_spell_target(self):
        tt = LIB.parent
        hits = {f.name: sum(l.split("--")[0].count("GetTarget(") for l in f.read_text().splitlines())
                for f in tt.iterdir() if f.is_file()}
        hits = {k: v for k, v in hits.items() if v}
        # tt_lib: the channel-only read; tt_telemetry: the player cast hook (fires at Spell::cast, target fresh).
        self.assertEqual(hits, {"tt_lib.ext": 1, "tt_telemetry.lua": 1})


if __name__ == "__main__":
    unittest.main()
