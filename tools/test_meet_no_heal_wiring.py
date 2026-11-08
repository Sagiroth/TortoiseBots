"""Wiring pins for DungeonRunSeekMeetNoHeal: healers skip the open Hakkar meet's puller and returner as heal targets.

The rule is pure and tested in tools/test_seek_aura_policy.cpp (SeekMeetHealHeld). These pins prove the live code uses
it: the key is read and documented, the query reads the same meet state ReadSeekFight does, and the heal-target value
asks the query for every candidate it adds.
Run: python3 -m unittest tools/test_meet_no_heal_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
HEAL_VALUE = (ROOT / "ai/playerbot/strategy/values/PartyMemberToHeal.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOC = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def query():
    return body(ACTIONS, "bool ai::SeekMeetWalkerHealHeld(")


def candidates():
    return body(HEAL_VALUE, "Unit* PartyMemberToHeal::Calculate()")


class MeetNoHealWiring(unittest.TestCase):
    def test_the_key_is_read_and_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetNoHeal", false)', CONFIG)
        self.assertIn("dungeonRunSeekMeetNoHeal = config.GetBoolDefault", CONFIG)
        self.assertIn("bool dungeonRunSeekMeetNoHeal = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekMeetNoHeal = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekMeetNoHeal`", SETTINGS)
        self.assertIn("(recommended `1`)", SETTINGS)
        self.assertIn("DungeonRunSeekMeetNoHeal", BOSS_DOC)
        self.assertIn("AiPlayerbot.DungeonRunSeekMeetNoHeal", CHANGELOG)

    def test_the_query_is_declared_for_the_heal_value(self):
        self.assertIn("bool SeekMeetWalkerHealHeld(std::uint32_t instanceId, std::uint64_t guid, float hpPct);", ACTIONS_H)

    def test_the_query_reads_the_meet_the_way_the_seek_does(self):
        fn = query()
        self.assertIn("SeekMeetSnapshot(instanceId, receiver, son, meetMs)", fn)
        self.assertIn("SeekMeetOpen(receiver, meetMs, WorldTimer::getMSTime())", fn)
        self.assertIn("SeekMeetWalkers(instanceId, puller, returner);", fn)
        self.assertIn("guid == puller || guid == returner", fn)

    def test_the_query_returns_before_any_lookup_with_the_key_off_or_a_hurt_target(self):
        fn = query()
        early = fn.index("sPlayerbotAIConfig.dungeonRunSeekMeetNoHeal, true, true, hpPct)")
        self.assertLess(early, fn.index("SeekMeetSnapshot("))
        self.assertLess(early, fn.index("SeekMeetWalkers("))
        self.assertIn("return SeekMeetHealHeld(sPlayerbotAIConfig.dungeonRunSeekMeetNoHeal, true, walker, hpPct);", fn)

    def test_the_heal_target_value_skips_held_targets(self):
        fn = candidates()
        add = fn[fn.index("auto addTarget = "):fn.index("// A focus-heal list must not leak")]
        self.assertIn("if (SeekMeetWalkerHealHeld(bot->GetInstanceId(), target->GetObjectGuid().GetRawValue(),", add)
        self.assertIn("target->GetHealthPercent()))", add)
        # After the plain validity check, before the candidate is built, so every pick through the value is covered.
        self.assertLess(add.index("if (!Check(target))"), add.index("SeekMeetWalkerHealHeld("))
        self.assertLess(add.index("SeekMeetWalkerHealHeld("), add.index("HealingCandidate candidate{"))
        self.assertIn('#include "playerbot/strategy/actions/BossTacticsActions.h"', HEAL_VALUE)

    def test_every_candidate_source_goes_through_the_one_add(self):
        fn = candidates()
        # The selection, the rpg target, the party and the quiet-seat helpers all add through addTarget; none builds a
        # HealingCandidate on its own, so none can bypass the hold.
        self.assertEqual(fn.count("HealingCandidate candidate{"), 1)
        self.assertEqual(fn.count("candidates.push_back("), 1)

    def test_a_self_heal_does_not_go_through_the_hold(self):
        # Check() drops the bot itself from this value, so a bot's own heal never reaches the query.
        check = body(HEAL_VALUE, "bool PartyMemberToHeal::Check(Unit* player)")
        self.assertIn("player == bot", check)


if __name__ == "__main__":
    unittest.main()
