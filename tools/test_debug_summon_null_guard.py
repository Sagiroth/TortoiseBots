"""Failed debug-marker summons must never be dereferenced.

SummonCreature returns nullptr once the core's active-summon cap is hit.
Debug marker code runs per path point (MovementAction::MoveTo2) or in bulk
(spell/transport/travel debug maps), so every use of the returned pointer --
aura attach, scale, passenger attach, teleport, guid read -- needs a guard,
and target-less lists must not be indexed blindly afterwards.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILES = {
    "ai/playerbot/PlayerbotAI.cpp": ["PlayerbotAI::AddAura"],
    "ai/playerbot/strategy/actions/DebugSpellCommands.cpp": ["DebugAction::addAura"],
    "ai/playerbot/strategy/actions/MovementActions.cpp": [
        "MovementAction::CreateWp",
        "MoveOnTransport",
        "MoveTo2",
    ],
    "ai/playerbot/strategy/actions/DebugTransportCommands.cpp": [
        "HandlePointOnTrans",
        "HandleOnTrans",
        "HandlePathable",
        "HandlePatharound",
    ],
    "ai/playerbot/strategy/actions/DebugTravelCommands.cpp": ["HandleShowNode"],
    "ai/playerbot/strategy/actions/TellLosAction.cpp": ["TellGameObjects"],
    "ai/playerbot/strategy/actions/WaitForAttackAction.cpp": ["GetBestPoint"],
}


def guarded_block(lines, summon_line):
    depth = 0
    seen_guard = False
    for line in lines[summon_line:summon_line + 40]:
        if re.search(r"if\s*\(\s*!?\s*(?:wpCreature|lCreature)\b", line):
            seen_guard = True
        for ch in line:
            if ch == "{":
                depth += 1
            elif ch == "}":
                if depth == 0 and seen_guard:
                    return True
                depth = max(0, depth - 1)
        if seen_guard and re.search(r"\bcontinue\b|\breturn\b", line):
            return True
    return False


class SummonNullGuardTest(unittest.TestCase):
    def test_aura_entry_points_reject_null(self):
        ai = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
        body = ai[ai.index("bool PlayerbotAI::AddAura"):]
        body = body[:body.index("\n}\n") + 3]
        self.assertIn("if (!unit)", body)
        spell = (ROOT / "ai/playerbot/strategy/actions/DebugSpellCommands.cpp").read_text()
        body = spell[spell.index("void DebugAction::addAura"):]
        body = body[:body.index("\n}\n") + 3]
        self.assertIn("if (!target)", body)

    def test_marker_derefs_are_guarded(self):
        deref = re.compile(r"\bwpCreature\s*(?:->|,|\)|\])")
        for rel in FILES:
            with self.subTest(file=rel):
                lines = (ROOT / rel).read_text().splitlines()
                for n, line in enumerate(lines):
                    if "SummonCreature(" not in line or "=" not in line:
                        continue
                    if "Creature*" not in line and "wpCreature =" not in line and "lCreature =" not in line:
                        continue
                    # Find the next dereference of the summon result.
                    for m in range(n + 1, min(n + 12, len(lines))):
                        if deref.search(lines[m]):
                            self.assertTrue(
                                guarded_block(lines, n),
                                f"{rel}:{n + 1} dereferences a summon result without a null guard "
                                f"(first deref at line {m + 1}: {lines[m].strip()})",
                            )
                            break


if __name__ == "__main__":
    unittest.main()
