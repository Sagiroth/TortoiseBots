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
POLICY_AURA = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def query():
    return body(ACTIONS, "std::uint64_t ai::SeekMeetHealPuller(")


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
        self.assertIn("std::uint64_t SeekMeetHealPuller(std::uint32_t instanceId);", ACTIONS_H)

    def test_the_query_reads_the_meet_under_one_lock(self):
        fn = query()
        # One lock for the receiver, the shot time and the puller together (no snapshot-then-walkers pair).
        self.assertEqual(fn.count("std::lock_guard<std::mutex> lock(g_walkoutMutex);"), 1)
        self.assertNotIn("SeekMeetSnapshot(", fn)
        self.assertNotIn("SeekMeetWalkers(", fn)
        self.assertIn("SeekMeetOpen(state.meetReceiver, state.meetMs, WorldTimer::getMSTime())", fn)
        self.assertIn("state.meetPuller", fn)
        # The returner is not read: an unrelated walk-out puller that became the returner is not held.
        self.assertNotIn("returner", fn)

    def test_the_query_returns_before_the_lock_with_the_key_off_or_no_meet_open(self):
        fn = query()
        gate = fn.index("if (!SeekMeetHealScope(sPlayerbotAIConfig.dungeonRunSeekMeetNoHeal,")
        self.assertLess(gate, fn.index("std::lock_guard<std::mutex> lock(g_walkoutMutex);"))
        self.assertIn("g_walkoutMeetsOpen.load(std::memory_order_relaxed)", fn[gate:gate + 200])
        self.assertIn("return 0;", fn[gate:gate + 260])
        # No map constant anywhere: the meet comes from seek_pull rows on any map.
        self.assertNotIn("mapId", fn)
        self.assertNotIn("kSeekMeetHealMapId", POLICY_AURA)
        self.assertNotIn("309", fn)

    def test_every_meet_open_close_and_drop_keeps_the_count(self):
        # The count of open meets must be exact on every path. The only writers of a record's meet are the open site,
        # the compare-and-clear and the record drop; each notes the change.
        self.assertEqual(ACTIONS.count("g_walkoutMeetsOpen.fetch_add("), 1)
        note = body(ACTIONS, "void NoteWalkoutMeet(bool before, const SeekWalkoutState& after)")
        self.assertIn("SeekMeetOpenDelta(before, after.meetReceiver != 0)", note)
        # Open: the receiver is set once, and the count is noted right after the four fields.
        self.assertEqual(ACTIONS.count("state.meetReceiver = receiver->GetObjectGuid().GetRawValue();"), 1)
        opened = ACTIONS[ACTIONS.index("bool const meetBefore = it->second.state.meetReceiver != 0;"):]
        self.assertLess(opened.index("it->second.state.meetReceiver = "),
                        opened.index("NoteWalkoutMeet(meetBefore, it->second.state);"))
        self.assertLess(opened.index("it->second.state.meetPuller = it->second.state.puller;"),
                        opened.index("NoteWalkoutMeet(meetBefore, it->second.state);"))
        # Close: the shared compare-and-clear is called only through the wrapper (both callers), never directly.
        self.assertEqual(ACTIONS.count("WalkoutMeetCloseIf("), 1)
        wrapper = body(ACTIONS, "bool CloseWalkoutMeetIf(SeekWalkoutState& state,")
        self.assertIn("bool const closed = WalkoutMeetCloseIf(state, receiver, son, meetMs);", wrapper)
        self.assertIn("NoteWalkoutMeet(before, state);", wrapper)
        self.assertEqual(ACTIONS.count("CloseWalkoutMeetIf(it->second.state,"), 2)
        # Drop: the one place a record is erased notes its meet before the erase.
        self.assertEqual(ACTIONS.count("g_walkouts.erase("), 1)
        drop = body(ACTIONS, "DropWalkoutRecord(\n    std::unordered_map<std::uint32_t, WalkoutRecord>::iterator it)")
        self.assertLess(drop.index("NoteWalkoutMeet(it->second.state.meetReceiver != 0, SeekWalkoutState());"),
                        drop.index("g_walkouts.erase(it)"))
        # Nothing else writes a shared meet field or empties the table.
        self.assertEqual(ACTIONS.count("g_walkouts.clear("), 0)
        self.assertEqual(ACTIONS.count("WalkoutMeetClear("), 1)  # the local reader copy only (not the shared state)
        self.assertIn("WalkoutMeetClear(state);", ACTIONS)

    def test_the_heal_value_reads_the_meet_once_per_pick(self):
        fn = candidates()
        self.assertEqual(fn.count("SeekMeetHealPuller("), 1)
        read = fn.index("SeekMeetHealPuller(bot->GetInstanceId())")
        # Before the candidate loops and before addTarget, never inside a per-candidate lambda.
        self.assertLess(read, fn.index("auto addTarget = "))
        self.assertLess(read, fn.index("for (Player* player : GetPartyMembers())"))

    def test_the_hold_uses_the_helpers_and_the_incoming_damage(self):
        fn = candidates()
        held = fn[fn.index("auto const meetHealHeld = "):fn.index("std::vector<HealingCandidate> candidates;")]
        self.assertIn("SeekMeetHealWalker(target->GetObjectGuid().GetRawValue(), meetPuller)", held)
        self.assertIn("return SeekMeetHealHeld(sPlayerbotAIConfig.dungeonRunSeekMeetNoHeal, true, true,", held)
        self.assertIn("SeekMeetHealHpAfterIncoming(target->GetHealthPercent(), getIncomingdamage(target),", held)
        self.assertIn("target->GetMaxHealth())", held)
        # A non-walker returns false before the incoming-damage scan.
        self.assertLess(held.index("return false;"), held.index("getIncomingdamage"))

    def test_the_heal_target_value_skips_held_targets_and_returns(self):
        fn = candidates()
        add = fn[fn.index("auto addTarget = "):fn.index("// A focus-heal list must not leak")]
        # The full shape: the check, the skip and nothing between them. Deleting the `return;` would let the `if` guard
        # the next block (issue #904) and keep the target.
        self.assertIn("        if (meetHealHeld(target))\n            return;\n", add)
        # After the plain validity check, before the candidate is built, so every pick through the value is covered.
        self.assertLess(add.index("if (!Check(target))"), add.index("if (meetHealHeld(target))"))
        self.assertLess(add.index("if (meetHealHeld(target))"), add.index("HealingCandidate candidate{"))
        self.assertIn('#include "playerbot/strategy/actions/BossTacticsActions.h"', HEAL_VALUE)

    def test_every_candidate_source_goes_through_the_one_add(self):
        fn = candidates()
        # The selection, the rpg target, the party and the quiet-seat helpers all add through addTarget; none builds a
        # HealingCandidate on its own, so none can bypass the hold.
        self.assertEqual(fn.count("HealingCandidate candidate{"), 1)
        self.assertEqual(fn.count("candidates.push_back("), 1)

    def test_a_self_heal_does_not_go_through_the_hold(self):
        # Check() drops the bot itself from this value, so a bot's own heal never reaches the hold.
        check = body(HEAL_VALUE, "bool PartyMemberToHeal::Check(Unit* player)")
        self.assertIn("player == bot", check)

    def test_the_docs_name_the_limits(self):
        self.assertIn("offheal", SETTINGS.lower())
        self.assertIn("Renew", SETTINGS)
        self.assertIn("pet", SETTINGS[SETTINGS.index("### `AiPlayerbot.DungeonRunSeekMeetNoHeal`"):])


if __name__ == "__main__":
    unittest.main()
