"""A shadow priest keeps Shadowform: the heal gate wiring (#1556).

Every priest heal sits behind a "remove shadowform" prerequisite, and the
engine checks isUseful() before pushing it -- so the gate in isUseful()
must see all 10 heal action classes, the Renew yield must see a covered
ally, and the wand stop must agree with the yield. Pins what stays:
the offheal medium row, the remove-shadowform nodes, the untouched
remove-shadowform action.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIEST = ROOT / "ai/playerbot/strategy/priest"
ACTIONS_H = (PRIEST / "PriestActions.h").read_text()
ACTIONS_CPP = (PRIEST / "PriestActions.cpp").read_text()
TRIGGERS_CPP = (PRIEST / "PriestTriggers.cpp").read_text()
STRATEGY = (PRIEST / "PriestStrategy.cpp").read_text()

GATED = (
    "CastLesserHealAction",
    "CastLesserHealOnPartyAction",
    "CastHealAction",
    "CastHealOnPartyAction",
    "CastGreaterHealAction",
    "CastGreaterHealOnPartyAction",
    "CastFlashHealAction",
    "CastFlashHealOnPartyAction",
    "CastRenewAction",
    "CastRenewOnPartyAction",
)


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ShadowformOffhealWiringTest(unittest.TestCase):
    def test_all_ten_heals_are_gated(self):
        for name in GATED:
            self.assertRegex(
                ACTIONS_H,
                r"class %s : public PriestFormGatedHeal<%sBase>" % (name, name),
                name,
            )
        self.assertIn("bool PriestShadowformHealGateOpen(", ACTIONS_H)

    def test_gate_probes_the_form_aside(self):
        self.assertIn("bool ai::PriestShadowformHealGateOpen(", ACTIONS_CPP)
        gate = body(ACTIONS_CPP, "bool ai::PriestShadowformHealGateOpen(")
        self.assertIn("GetErrorAtShapeshiftedCast", gate)
        self.assertIn("SpellGlobalCooldownRemainingMs", gate)
        self.assertIn("GetAttackTimer(RANGED_ATTACK)", gate)

    def test_renew_yields_to_covering_healer(self):
        renew = body(ACTIONS_CPP, "bool CastRenewOnPartyAction::isUseful()")
        self.assertIn("ShadowformOffhealYields", renew)
        self.assertIn("AnotherHealerCoversAlly", renew)

    def test_wand_stop_agrees_with_yield(self):
        stop = body(
            TRIGGERS_CPP, "bool PriestWandBlocksCastTrigger::IsActive()"
        )
        self.assertIn("ShadowformOffhealYields", stop)
        self.assertIn("AnotherHealerCoversAlly", stop)

    def test_offheal_medium_row_still_renews(self):
        self.assertRegex(
            STRATEGY,
            r'"party member medium health",\s*NextAction::array\(0, new NextAction\("renew on party", ACTION_LIGHT_HEAL\)',
        )

    def test_remove_shadowform_nodes_stay(self):
        self.assertIn(
            'ACTION_NODE_P(renew_on_party, "renew on party", "remove shadowform")',
            STRATEGY,
        )

    def test_remove_shadowform_action_untouched(self):
        action = body(ACTIONS_H, "class CastRemoveShadowformAction")
        self.assertIn('ai->HasAura("shadowform", AI_VALUE(Unit*, "self target"))', action)
        self.assertNotIn("PriestFormGatedHeal", action)
        self.assertNotIn("PriestShadowformHealGateOpen", action)


if __name__ == "__main__":
    unittest.main()
