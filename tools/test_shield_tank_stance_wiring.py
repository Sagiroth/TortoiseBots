"""Shield tank stance wiring (issue #1553): a levelling Arms/Fury warrior
tanking in Defensive Stance, or a druid in Bear/Dire Bear Form, counts as a
tank at the shield gate even when the talent-role read says DPS. Round 3: the
explicit-role veto lives in one shared helper next to IsTank
(PlayerbotAI::HasExplicitNonTankRole), used by both IsTank and the shield
guard so the two can't diverge. A player's stored role always counts; a bot
AI's role counts only with an active master.

Run: python3 -m unittest tools/test_shield_tank_stance_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = ROOT / "ai/playerbot/strategy/priest/PriestActions.h"
AI_CPP = ROOT / "ai/playerbot/PlayerbotAI.cpp"
AI_H = ROOT / "ai/playerbot/PlayerbotAI.h"


def shield_body():
    text = ACTIONS.read_text()
    body = text[text.index("class CastPowerWordShieldOnPartyAction"):]
    return body[:body.index("\n    };")]


def cpp_fn(name):
    text = AI_CPP.read_text()
    body = text[text.index("bool PlayerbotAI::%s(" % name):]
    return body[:body.index("\n}\n")]


class ShieldTankStanceWiringTest(unittest.TestCase):
    def test_warrior_stance_term(self):
        # CLASS_WARRIOR stays paired with aura 71 through &&: swapping the
        # class or weakening && to || breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"target->IsPlayer\(\)\s*&&\s*\(\(Player\*\)target\)->GetClass\(\)"
            r"\s*==\s*CLASS_WARRIOR\s*&&\s*ai->HasAura\(71,",
        )

    def test_druid_form_term(self):
        # CLASS_DRUID stays paired with Bear 5487 or Dire Bear 9634 through
        # &&: swapping the class or weakening && to || breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"target->IsPlayer\(\)\s*&&\s*\(\(Player\*\)target\)->GetClass\(\)"
            r"\s*==\s*CLASS_DRUID\s*&&\s*\(ai->HasAura\(5487,\s*target\)"
            r"\s*\|\|\s*ai->HasAura\(9634,",
        )

    def test_guard_uses_shared_helper(self):
        # The guard delegates the veto to the shared helper, still guarded
        # by IsPlayer, fed to TankForShield between the role read and the
        # stance checks. Re-inlining the storage reads breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"TankForShield\([\s\S]*?IsTank\(\(Player\*\)target"
            r"[\s\S]*?target->IsPlayer\(\)\s*&&\s*"
            r"PlayerbotAI::HasExplicitNonTankRole\(\(Player\*\)target\)"
            r"[\s\S]*?CLASS_WARRIOR",
        )
        self.assertNotIn("GetPlayerForcedRole", body)

    def test_guard_reads_no_combat_state(self):
        # The rage-tank rule holds in or out of combat, so the guard reads no
        # combat state: a shield cast between pulls still eats the next pull's
        # opening hits. Adding a combat input back breaks this pin.
        body = shield_body()
        self.assertNotIn("TankEngagedForShield", body)
        self.assertNotIn("IsInCombat", body)
        self.assertRegex(
            body,
            r"target->GetPowerType\(\) == POWER_RAGE,\s*"
            r"\(unsigned\)target->GetHealthPercent\(\),\s*"
            r"sPlayerbotAIConfig\.criticalHealth",
        )


class ExplicitNonTankRoleWiringTest(unittest.TestCase):
    def test_helper_declared_next_to_istank(self):
        decls = AI_H.read_text()
        self.assertIn("static bool HasExplicitNonTankRole(Player* player);", decls)
        self.assertLess(
            decls.index("static bool IsTank(Player* player"),
            decls.index("static bool HasExplicitNonTankRole(Player* player);"),
        )

    def test_helper_storage_role_first(self):
        # A player's stored forced role decides whenever set: TANK reads as
        # no veto, anything else as a veto. Reading the bot AI first breaks
        # this pin.
        fn = cpp_fn("HasExplicitNonTankRole")
        self.assertRegex(
            fn,
            r"GetPlayerForcedRole\(player->GetObjectGuid\(\)\)[\s\S]*?"
            r"if\s*\(forcedRole != .*BOT_ROLE_NONE\)\)"
            r"\s*return forcedRole != .*BOT_ROLE_TANK\);",
        )
        self.assertLess(fn.index("GetPlayerForcedRole"), fn.index("GetAI(player)"))

    def test_helper_bot_role_needs_active_master(self):
        # A bot AI's forced role vetoes only with an active master: an
        # ordered DPS in stance is spared the veto once the master goes
        # away. Dropping the master gate breaks this pin.
        fn = cpp_fn("HasExplicitNonTankRole")
        self.assertRegex(
            fn,
            r"botForcedRole != .*BOT_ROLE_NONE\)\s*&&\s*"
            r"botAi->HasActivePlayerMaster\(\)\)",
        )

    def test_istank_uses_shared_helper(self):
        # IsTank keeps its stored-role early return, its TANK early return
        # and its tank-strategy read, delegating only the owned-companion
        # veto to the helper. Behaviour unchanged; bypassing the helper
        # breaks this pin.
        fn = cpp_fn("IsTank")
        self.assertRegex(
            fn,
            r"GetPlayerForcedRole\(player->GetObjectGuid\(\)\)[\s\S]*?"
            r"if\s*\(forcedRole != .*BOT_ROLE_NONE\)\)"
            r"\s*return forcedRole == .*BOT_ROLE_TANK\);",
        )
        self.assertIn("ContainsStrategy(STRATEGY_TYPE_TANK)", fn)
        self.assertIn("if (HasExplicitNonTankRole(player))", fn)
        self.assertNotIn("HasActivePlayerMaster", fn)


if __name__ == "__main__":
    unittest.main()
