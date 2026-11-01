"""Spec 03: healers never spend a GCD on a damage item.

The gate sits in the action (the single choke point): ThrowGrenadeAction,
UseGoblinSapperChargeAction and UseOilOfImmolationAction each refuse when
the bot is in the heal role. Trigger rows stay, so dps keep the items.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.h").read_text()
GENERIC = (ROOT / "ai/playerbot/strategy/generic/ClassStrategy.cpp").read_text()

GATE_IF = "if (!DamageItemAllowed(ai->IsHeal(bot)))"
GATE_RETURN = "return DamageItemAllowed(ai->IsHeal(bot)) &&"


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body_from(text, start):
    i = text.index(start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


SRC = strip_comments(ACTIONS)


class HealerItemWiringTest(unittest.TestCase):
    def test_grenade_refuses_for_healers(self):
        self.assertIn(GATE_IF, body_from(SRC, "class ThrowGrenadeAction"))

    def test_sapper_refuses_for_healers(self):
        self.assertIn(GATE_RETURN, body_from(SRC, "class UseGoblinSapperChargeAction"))

    def test_oil_refuses_for_healers(self):
        self.assertIn(GATE_IF, body_from(SRC, "class UseOilOfImmolationAction"))

    def test_policy_header_included(self):
        self.assertIn("playerbot/policy/combat/HealerItemPolicy.h", ACTIONS)

    def test_aoe_strategy_still_registers_grenade_for_dps(self):
        self.assertIn('"throw grenade"', strip_comments(GENERIC))


if __name__ == "__main__":
    unittest.main()
