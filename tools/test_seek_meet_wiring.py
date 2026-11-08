"""Wiring pins for the pulled Son's meet (Hakkar's walk-out brings the Son to a waiting off-tank) and the Cause
Insanity row.

The rules are pure and tested in tools/test_seek_walkout_policy.cpp (the receiver pick and its refusals, the meet's
back point and arrival, its 20 s clock) and tools/test_seek_aura_policy.cpp (the take row's reason words). These pins
prove the live code calls them: the shot names a receiver, the walk back ends at that receiver, the receiver takes the
Son on arrival, and the two new rows carry their fixed words.
Run: python3 -m unittest tools/test_seek_meet_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOC = (ROOT / "docs/play/refusal-rows.md").read_text()
FIGHT_STATE = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def inline_body(text, signature):
    """A namespace-level inline function of a header (its body closes at the 4-space brace)."""
    start = text.index(signature)
    return text[start:text.index("\n    }\n", start)]


def update():
    return body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")


def tank_pick():
    return body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")


def execute():
    return body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")


def walkout_walk():
    fn = execute()
    return fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]


class SeekSonMeetWiring(unittest.TestCase):
    def test_the_flag_is_read_as_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekSonMeet", false)', CONFIG)
        self.assertIn("dungeonRunSeekSonMeet = config.GetBoolDefault", CONFIG)
        self.assertIn("bool dungeonRunSeekSonMeet = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekSonMeet = 0", TEMPLATE)

    def test_the_meet_is_armed_by_the_seek_pull_row_and_the_flag(self):
        fn = update()
        self.assertIn("const bool meetOn = SeekPullRulesOn(bot->GetMapId(), &row) &&"
                      " sPlayerbotAIConfig.dungeonRunSeekSonMeet;", fn)

    def test_the_walk_back_ends_at_the_receiver(self):
        walk = walkout_walk()
        # The meet-aware point is asked only with the new flag, a seek_pull row and an open meet whose receiver is
        # in the world; the old circle-edge point stays as the fallback.
        self.assertIn("Player* const receiver = sPlayerbotAIConfig.dungeonRunSeekSonMeet &&", walk)
        self.assertIn("SeekPullRulesOn(bot->GetMapId(), task.row) &&", walk)
        self.assertIn("SeekMeetSnapshot(bot->GetInstanceId(), meetReceiver, meetSon, meetMs) &&", walk)
        self.assertIn("SeekMeetOpen(meetReceiver, meetMs, WorldTimer::getMSTime())", walk)
        self.assertIn("WalkoutMeetBackPoint(true, receiver->GetPositionX(), receiver->GetPositionY(), tank,", walk)
        self.assertIn("WalkoutBackPoint(tank, bot->GetPositionX(), bot->GetPositionY(), x, y);", walk)
        self.assertLess(walk.index("WalkoutMeetBackPoint(true,"), walk.index("WalkoutBackPoint(tank,"))

    def test_the_puller_arrives_at_the_receiver(self):
        fn = update()
        # Both the running task's back and the aborted puller's walk back use the meet's arrival rule.
        self.assertIn("meetOn && SeekMeetOpen(state.meetReceiver, state.meetMs, nowMs) && meetReceiver &&",
                      fn)
        self.assertIn("WalkoutMeetBackPoint(true, meetReceiver->GetPositionX(), meetReceiver->GetPositionY(), tank,",
                      fn)
        self.assertIn("WalkoutMeetArrived(true, px, py, tank,", fn)
        self.assertGreaterEqual(fn.count("WalkoutMeetArrived(true,"), 2)
        self.assertIn("WalkoutBackArrived(tank, puller->GetPositionX(), puller->GetPositionY());", fn)
        returner = fn[fn.index("The walk back after a timeout or an abort"):]
        self.assertIn("WalkoutMeetArrived(true, px, py, tank,", returner)

    def test_the_receiver_is_picked_on_the_shots_first_edge(self):
        fn = update()
        self.assertIn("bool const firstShot = !it->second.state.shot;", fn)
        edge = fn[fn.index("bool const firstShot = !it->second.state.shot;"):]
        self.assertLess(edge.index("WalkoutNoteShot(it->second.state);"), edge.index("if (firstShot && meetOn)"))
        self.assertIn("PickMeetReceiver(tanks)", edge)
        self.assertEqual(1, ACTIONS.count("PickMeetReceiver("))
        # The pick's event row, or its refusal counts when no tank could wait.
        self.assertIn("MeetRefusalCounts(tanks)", edge)

    def test_the_receiver_branch_precedes_the_other_tank_branches(self):
        fn = tank_pick()
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunSeekSonMeet &&"
                      " SeekPullRulesOn(bot->GetMapId(), state.row) &&", fn)
        self.assertIn("SeekMeetSnapshot(bot->GetInstanceId(), meetReceiver, meetSon, meetMs) &&", fn)
        self.assertIn("SeekMeetOpen(meetReceiver, meetMs, WorldTimer::getMSTime())", fn)
        self.assertIn("SeekMeetTakeDue(bot->GetDistance(son), false)", fn)
        self.assertIn("task.meet = true;", fn)
        meet = fn.index("SeekMeetSnapshot(bot->GetInstanceId(), meetReceiver, meetSon, meetMs)")
        self.assertLess(meet, fn.index("if (!thirdTank && !pickupTank && !healerTaker && state.walkout)"))
        self.assertLess(meet, fn.index("if (thirdTank || pickupTank)\n"))
        # The wait is a dead end for the walker's Son only: no other Son, pull or drag while the Son's victim is the
        # puller, the returner or nobody.
        wait = fn[fn.index("Waiting is for the walker's Son only"):]
        self.assertIn("SeekMeetWalkers(bot->GetInstanceId(), meetPuller, meetReturner);", wait)
        self.assertIn("MeetReceiverShouldWait(sonVictim ? sonVictim->GetObjectGuid().GetRawValue() : 0, meetPuller,", wait)
        self.assertIn("LogSeekMeetWait(ai, bot, son, sonVictim, meetMs);", wait)
        self.assertIn("task.valid = false;", wait)
        # Anyone else's Son closes the meet as `peeled` and falls through to today's take branches.
        self.assertIn("SeekMeetClose(bot->GetInstanceId(), meetReceiver, meetMs)", wait)
        self.assertIn('LogSeekMeetLost(ai, meetSon, "peeled", WorldTimer::getMSTime() - meetMs);', wait)
        self.assertLess(wait.index("task.valid = false;"), wait.index('"peeled"'))
        self.assertLess(wait.index('"peeled"'), wait.index("task.row = nullptr;"))
        self.assertIn("event=wait,reason=receiver_wait", ACTIONS)
        self.assertIn("g_seekMeetWaitLog[bot->GetObjectGuid().GetRawValue()]", ACTIONS)

    def test_the_receiver_is_revalidated_every_update(self):
        fn = update()
        end = fn.index("if (WalkoutActive(state))")
        meet = fn[:end]
        self.assertIn("MeetReceiverLostWhy(!meetReceiver->GetCharmerGuid().IsEmpty() ||", meet)
        self.assertIn("meetReceiver->HasAura(kHakkarCauseInsanitySpell),", meet)
        self.assertIn("boss->GetVictim() == meetReceiver,", meet)
        self.assertIn("PlayerHoldsFightSon(fightSons, row.targetEntry, meetReceiver,", meet)
        self.assertIn("LogSeekMeetLost(ai, guid, lost, nowMs - state.meetMs);", meet)
        # The loss clears the meet, so the puller walks to the circle edge again.
        self.assertIn("WalkoutMeetClear(it->second.state);", meet)

    def test_holds_son_scans_the_fight_sons(self):
        fn = update()
        self.assertIn("candidate.holdsSon = PlayerHoldsFightSon(fightSons, row.targetEntry, member, 0);", fn)
        helper = body(ACTIONS, "bool PlayerHoldsFightSon(")
        self.assertIn("son->GetVictim() == member", helper)
        self.assertIn("sServerFacade.IsAlive(son)", helper)
        # The fight Sons (with the sole-candidate grid search) are built before the walk-out reads them.
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertLess(read.index("std::vector<Unit*> fightSons = out.sons;"), read.index("UpdateSeekWalkout(ai,"))

    def test_the_take_row_says_why(self):
        self.assertIn("seek take: %s -> %u victim=%s reason=%s meet=%d", ACTIONS)
        take = execute()
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        self.assertIn("const bool canTaunt = taunt && ai->CanCastSpell(taunt, task.source);", take)
        self.assertIn("LogSeekTake(ai, bot, task.source, SeekTakeReasonWord(chaseRefused, taunt != nullptr,"
                      " canTaunt), task.meet);", take)
        self.assertLess(take.index("const bool canTaunt ="), take.index("LogSeekTake("))
        self.assertIn("bool meet = false;", ACTIONS_H)

    def test_the_meet_rows(self):
        for row in ('"son=%u,event=meet,receiver=%s,tank_idx=%d,receiver_yd=%.1f,due_in=%u"',
                    '"son=%u,event=no_meet,reason=%s,tanks=%d"',
                    '"son=%u,event=taken,receiver=%s,by=%s,after_ms=%u"',
                    '"son=%u,event=lost,reason=%s,after_ms=%u"',
                    '"son=%u,event=wait,reason=receiver_wait,victim=%s,son_yd=%.1f,after_ms=%u"'):
            self.assertIn(row, ACTIONS)
        self.assertIn('"BossTacticsSonMeet"', ACTIONS)
        self.assertIn("LogSeekMeet(ai, meetRow.c_str());", ACTIONS)
        # Every meet end is checked on every read, task or not.
        fn = update()
        for word in ('lost = "son_dead";', 'lost = "receiver_dead";', 'lost = "timeout";'):
            self.assertIn(word, fn)
        self.assertIn("WalkoutMeetClear(it->second.state);", fn)
        self.assertIn('"son=%u,event=lost,reason=%s,after_ms=%u"', ACTIONS)
        self.assertNotIn("meetTakenLogged", ACTIONS)


class CauseInsanityRowWiring(unittest.TestCase):
    def read(self):
        return body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")

    def test_the_row_is_built(self):
        for field in ('"charmed=%s:%s,victim=%s:%s,ot1=%.0f,ot2=%.0f,top=%s"',
                      '"Cause Insanity on %s: Hakkar turned to %s (%s)"',
                      '"BossTacticsCharm"'):
            self.assertIn(field, ACTIONS)
        self.assertIn("LogSeekCharm(ai, bot, boss, charmRowGuid, roles);", ACTIONS)

    def test_the_scan_waits_500_ms_and_the_row_1500_ms(self):
        read = self.read()
        self.assertIn("nowMs - record.charmScanMs >= 500", read)
        self.assertIn("HasAura(kHakkarCauseInsanitySpell)", read)
        self.assertIn("charmRowGuid = record.charmGuid;", read)
        # The 1.5 s wait and the one-row latch are the policy's (SeekCharmRowDue), pinned in its own test.
        self.assertIn("constexpr std::uint32_t kSeekCharmWaitMs = 1500;", FIGHT_STATE)
        self.assertIn("nowMs - record.charmSeenMs < kSeekCharmWaitMs", FIGHT_STATE)
        self.assertIn("record.charmLogged = true;", FIGHT_STATE)
        # The scan records the sighting, then the wait gates the row.
        scan = read.index("SeekCharmScan(record, charmed, nowMs);")
        due = read.index("if (SeekCharmRowDue(record, nowMs))")
        emit = read.index("charmRowGuid = record.charmGuid;")
        self.assertLess(scan, due)
        self.assertLess(due, emit)

    def test_a_scan_with_nobody_charmed_forgets_the_last_guid(self):
        read = self.read()
        self.assertIn("SeekCharmScan(record, charmed, nowMs);", read)
        edge = inline_body(FIGHT_STATE, "inline void SeekCharmScan(")
        self.assertIn("if (charmed == 0)", edge)
        for reset in ("record.charmGuid = 0;", "record.charmSeenMs = 0;", "record.charmLogged = false;"):
            self.assertIn(reset, edge[:edge.index("else if")])

    def test_the_scan_only_judges_members_in_the_bots_world_map_and_instance(self):
        read = self.read()
        scan = read[read.index("nowMs - record.charmScanMs >= 500"):read.index("SeekCharmScan(record, charmed, nowMs);")]
        self.assertIn("!member->IsInWorld() || member->GetMapId() != bot->GetMapId() ||", scan)
        self.assertIn("member->GetInstanceId() != bot->GetInstanceId()", scan)
        self.assertLess(scan.index("member->GetInstanceId() != bot->GetInstanceId()"),
                        scan.index("member->HasAura(kHakkarCauseInsanitySpell)"))

    def test_the_top_is_bounded_and_leaves_the_charmed_player_out(self):
        self.assertIn("i < 5", ACTIONS)
        self.assertIn("GetObjectGuid().GetRawValue() == charmedGuid", ACTIONS)
        self.assertIn("getThreatList()", ACTIONS)
        self.assertIn("roles.tanks.size() > 1 && roles.tanks[1]", ACTIONS)
        self.assertIn("roles.tanks.size() > 2 && roles.tanks[2]", ACTIONS)


class RefusalDocWiring(unittest.TestCase):
    def test_every_new_word_is_documented(self):
        for word in ("dead", "charmed", "victim", "busy", "outside", "none", "son_dead", "receiver_dead",
                     "timeout", "chase_refused", "no_taunt", "attack", "taunt", "peeled", "receiver_charmed",
                     "receiver_victim", "receiver_busy", "receiver_wait"):
            self.assertIn("`%s`" % word, DOC, word)


if __name__ == "__main__":
    unittest.main()
