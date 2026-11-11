"""The `react` strategy exists, and only behind AiPlayerbot.RaidReactions.

Every reaction engine asked for a strategy named `react` (AiFactory) that no
strategy context created, so Engine::addStrategy skipped the unknown name in
silence and the five universal raid rows never existed (#1589). The creator
now exists; the rows live in a pure policy list (ReactionRowsPolicy.h) gated by
the key, the four state rows stay with ClassStrategy, and the legacy raid
spread action's stay gate reads the same key.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY_CONTEXT = (ROOT / "ai/playerbot/strategy/StrategyContext.h").read_text()
AIFACTORY = (ROOT / "ai/playerbot/AiFactory.cpp").read_text()
REACTION = (ROOT / "ai/playerbot/strategy/generic/ReactionStrategy.cpp").read_text()
REACTION_H = (ROOT / "ai/playerbot/strategy/generic/ReactionStrategy.h").read_text()
CLASS_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ClassStrategy.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/ReactionRowsPolicy.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DUNGEON_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ReactStrategyWiringTest(unittest.TestCase):
    def test_react_has_a_creator(self):
        self.assertIn('#include "generic/ReactionStrategy.h"', STRATEGY_CONTEXT)
        self.assertIn(
            'creators["react"] = [](PlayerbotAI* ai) { return new ReactionStrategy(ai); };',
            STRATEGY_CONTEXT)

    def test_every_reaction_engine_still_asks_for_react(self):
        # The call is unchanged: the missing creator was the whole bug.
        self.assertRegex(AIFACTORY, r'AddDefaultReactionStrategies[\s\S]{0,400}addStrategies\("react",')
        # ... and every class hands its reaction engine a class strategy, so the
        # four state rows have a home once `react` stops carrying them.
        fn = body(AIFACTORY, "void AiFactory::AddDefaultReactionStrategies(", "ReactionEngine* AiFactory::createReactionEngine(")
        for name in ('"elemental"', '"enhancement"', '"restoration"', '"assassination"',
                     '"subtlety"', '"combat"', '"affliction"', '"demonology"', '"destruction"',
                     '"arcane"', '"fire"', '"frost"', '"holy"', '"protection"', '"retribution"',
                     '"arms"', '"fury"', '"balance"', '"tank feral"', '"dps feral"',
                     '"beast mastery"', '"marksmanship"', '"survival"', '"discipline"',
                     '"shadow"', '"leveling"'):
            self.assertIn(f"reactionEngine->addStrategy({name})", fn, name)

    def test_reaction_strategy_holds_no_state_rows(self):
        fn = body(REACTION, "void ReactionStrategy::InitReactionTriggers(", "\n}\n")
        for trig in ('"combat start"', '"combat end"', '"death"', '"resurrect"'):
            self.assertNotIn(trig, fn)
        # ClassStrategy keeps them.
        cls = body(CLASS_STRATEGY, "void ClassStrategy::InitReactionTriggers(", "\n}\n")
        for trig in ('"combat start"', '"combat end"', '"death"', '"resurrect"'):
            self.assertIn(trig, cls)
        self.assertNotIn("TO DO: Delete this", REACTION_H)
        # The engine keys the strategy map and every removal by getName(), so
        # the class must name itself exactly what AiFactory asks for.
        self.assertIn('std::string getName() override { return "react"; }', REACTION_H)

    def test_the_rows_read_the_key(self):
        self.assertIn("ReactionRaidRows(sPlayerbotAIConfig.raidReactions)", REACTION)
        self.assertIn("std::vector<ReactionRaidRow> ReactionRaidRows(bool enabled)", POLICY)
        self.assertRegex(POLICY, r"if \(!enabled\)\s*\n\s*return \{\};")
        # The key is read at boot only: the lists are built when an engine inits.
        self.assertIn('raidReactions = config.GetBoolDefault("AiPlayerbot.RaidReactions", false);', CONFIG_CPP)
        self.assertIn("AiPlayerbot.RaidReactions = 0", TEMPLATE)

    def test_spread_stay_gate_reads_the_key(self):
        fn = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::isUseful()", "\n}\n")
        self.assertIn("IsRaidSpreadStayExempt(sPlayerbotAIConfig.raidReactions,", fn)
        self.assertIn("MovementAction::isUseful()", fn)
        # The base gate refuses on the stay of ai->GetState(); the exemption
        # must read the same state, or a stay kept elsewhere would exempt a
        # bot the gate refused.
        self.assertIn('ai->HasStrategy("stay", ai->GetState())', fn)
        # The trigger and Execute read the order through the same helper, so the
        # two cannot drift apart.
        self.assertIn("ShouldSpreadStayVeto(ai->HasStrategy(\"stay\", BotState::BOT_STATE_COMBAT), spreadLive)",
            DUNGEON_ACTIONS)
        triggers = (ROOT / "ai/playerbot/strategy/triggers/DungeonTriggers.cpp").read_text()
        self.assertIn("ShouldSpreadStayVeto(ai->HasStrategy(\"stay\", BotState::BOT_STATE_COMBAT), spreadLive)",
            triggers)


if __name__ == "__main__":
    unittest.main()
