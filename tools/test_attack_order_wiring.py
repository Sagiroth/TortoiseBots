"""Only explicit attack orders count "already attacking it" as done.

runtime/AttackOrderPolicy.h decides when an order on the bot's current victim
is already fulfilled. A success ends the engine tick, so automatic callers
must keep Unit::Attack's raw result: "melee" (relevance 5 on several tank and
dps strategies) succeeding every combat tick starved unstuck, return to stay
position and stay, and "razorgore engage" relies on the false as its victim
guard. The melee action instead goes USELESS while it already swings at its
target and faces it, so the tick falls through without "melee - FAILED" spam.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = ROOT / "ai/playerbot/strategy/actions"
ATTACK = (ACTIONS / "AttackAction.cpp").read_text()
GENERIC = (ACTIONS / "GenericActions.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class AttackOrderWiringTest(unittest.TestCase):
    def test_attack_accepts_current_victim_only_on_request(self):
        fn = body(ATTACK, "bool AttackAction::Attack(Player* requester, Unit* target, bool acceptCurrentVictim)", "\n}\n")
        self.assertIn("const bool alreadyAttacking = acceptCurrentVictim && TortoiseBots::IsAlreadyAttackingTarget(", fn)
        header = (ACTIONS / "AttackAction.h").read_text()
        self.assertIn("bool Attack(Player* requester, Unit* target, bool acceptCurrentVictim = false);", header)

    def test_only_explicit_orders_pass_true(self):
        calls = re.findall(r"Attack\(requester, [^)]*\)\)?", ATTACK)
        explicit = [c for c in calls if ", true)" in c]
        self.assertEqual(sorted(explicit), sorted(["Attack(requester, tgt, true))", "Attack(requester, rtiTarget, true))"]))
        mine = body(ATTACK, "bool AttackMyTargetAction::Execute(", "\n}\n")
        self.assertIn("Attack(requester, tgt, true)", mine)
        rti = body(ATTACK, "bool AttackRTITargetAction::Execute(", "\n}\n")
        self.assertIn("Attack(requester, rtiTarget, true)", rti)
        for path in ACTIONS.glob("*.cpp"):
            if path.name == "AttackAction.cpp":
                continue
            self.assertNotRegex(path.read_text(), r"(?<![>.])\bAttack\(requester,[^;]*,\s*true\)", path.name)

    def test_melee_useless_while_swinging_and_facing(self):
        fn = body(GENERIC, "bool MeleeAction::isUseful()", "\n}\n")
        self.assertIn("bot->GetVictim() == target", fn)
        self.assertIn("TortoiseBots::IsAlreadyAttackingTarget(true, ArmsMeleeSwing(), bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING))", fn)
        self.assertIn("isInFront(bot, target", fn)
        self.assertIn("return false;", fn)


if __name__ == "__main__":
    unittest.main()
