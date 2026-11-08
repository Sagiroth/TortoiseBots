"""Wiring pins for the nearest take (AiPlayerbot.DungeonRunSeekNearestTake, Hakkar walk 55 F3).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (SeekNearestTakerIs, SeekNearestTakeSonOk). These
pins prove FindBossSeekTank calls them under the flag, in the first off-tank's branch and before today's 3 s take, and
that the flag defaults off.
Run: python3 -m unittest tools/test_seek_nearest_take_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekNearestTakeWiring(unittest.TestCase):
    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekNearestTake", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekNearestTake = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekNearestTake = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekNearestTake`", SETTINGS)

    def test_find_boss_seek_tank_calls_the_helper_under_the_flag(self):
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        healer = find[find.index("    if (healerTaker)\n    {"):]
        gate = healer.index("if (sPlayerbotAIConfig.dungeonRunSeekNearestTake)")
        call = healer.index("FindSeekNearestTake(ai, bot, roles, state)")
        old = healer.index("if (state.healerSon)")
        self.assertLess(gate, call)
        self.assertLess(call, old)
        self.assertIn("task.source = nearestSon;", healer)
        # Flag off: today's 3 s rule is the branch's only take.
        self.assertIn("task.source = state.healerSon;", healer[old:])
        # The boss-victim stand-down still runs before any take.
        self.assertLess(find.index("!SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot)"),
                        find.index("FindSeekNearestTake(ai, bot, roles, state)"))

    def test_the_helper_reads_the_policy(self):
        fn = body(ACTIONS, "Unit* FindSeekNearestTake(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("SeekNearestTakerIs(roles.botIndex, 2, receiverYd, receiverTauntReady, selfYd, selfTauntReady,", fn)
        self.assertIn("selfIsBossVictim, selfHoldsSon)", fn)
        self.assertIn("SeekNearestTakeSonOk(victimNonTank, meetSon, inside)", fn)
        self.assertIn("const bool meetSon = state.meetOpen && entry.first == state.meetSon;", fn)
        self.assertIn("raid.radius += kSeekTakeEdgeSlackYd;", fn)
        self.assertIn("AnchorCircleOf(*anchorRow, false)", fn)
        self.assertIn("const bool selfIsBossVictim = state.boss && state.boss->GetVictim() == bot;", fn)
        self.assertIn("PlayerHoldsFightSon(state.sons, state.row->targetEntry, bot, 0)", fn)
        self.assertIn("Player* const receiver = roles.tanks[2];", fn)
        ready = body(ACTIONS, "bool SeekTankTauntReady(Player* tank)")
        self.assertIn("!tank->HasSpellCooldown(spellId)", ready)


if __name__ == "__main__":
    unittest.main()
