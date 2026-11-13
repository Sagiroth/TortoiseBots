"""An LFT-filled pool bot in a group keeps the role the dungeon finder gave it (#1605).

`RandomBotService::Update` re-applies `AiPlayerbot.RandomBotCombatStrategies`
and `RandomBotNonCombatStrategies` through `RandomBotFacade::ChangeStrategy`
every Min..MaxRandomBotChangeStrategyTime. It must not do that for a bot whose
role the dungeon finder (or `.bot role` / the hire provisioner) set while the
bot is in a group: the list's "+dps" is the class DPS alias (shadow for a
priest) and Engine::addStrategy drops the sibling spec, which turned a filled
healer into a shadow priest mid-dungeon.

The pure verdict is TimedReapplyHoldsRole (tools/test_timed_strategy_reapply_policy.cpp);
this checks the call site asks it, with the bot's own forced role and group,
before it touches either engine.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FACADE = (ROOT / "runtime/PlayerbotRuntimeFacade.cpp").read_text()
SERVICE = (ROOT / "runtime/RandomBotService.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TimedStrategyReapplyWiringTest(unittest.TestCase):
    def change_strategy(self):
        return strip_comments(body(FACADE, "void RandomBotFacade::ChangeStrategy(Player* player)", "\n}\n"))

    def test_facade_asks_the_policy(self):
        self.assertIn('#include "runtime/policy/infra/TimedStrategyReapplyPolicy.h"', FACADE)
        fn = self.change_strategy()
        self.assertIn("TimedReapplyHoldsRole(", fn)
        # The verdict reads the bot's own role and its group.
        self.assertIn("GetForcedRole()", fn)
        self.assertIn("player->GetGroup()", fn)

    def test_skip_comes_before_any_strategy_change(self):
        fn = self.change_strategy()
        guard = fn.index("TimedReapplyHoldsRole(")
        self.assertLess(guard, fn.index("ai->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies"))
        self.assertLess(guard, fn.index("ai->ChangeStrategy(AiFactory::CommandedSafeNonCombatStrategies("))
        # A verdict that skips must leave both engines untouched: the early
        # return is between the guard and the first ChangeStrategy.
        between = fn[guard:fn.index("ai->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies")]
        self.assertIn("return;", between)

    def test_timer_still_drives_the_facade(self):
        # The timer call site is unchanged: the timer still resets the bot's
        # age after the call, so a skipped re-apply waits a full interval.
        upd = strip_comments(body(SERVICE, "void RandomBotService::Update(uint32_t diff)", "\n}\n"))
        self.assertIn("sRandomBotFacade.ChangeStrategy(player);", upd)
        self.assertLess(upd.index("sRandomBotFacade.ChangeStrategy(player);"),
                        upd.index("m_strategyAgeMs[i] = 0;", upd.index("sRandomBotFacade.ChangeStrategy(player);")))


if __name__ == "__main__":
    unittest.main()
