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
POLICY = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
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
        self.assertIn('SeekMeetClose(bot->GetInstanceId(), meetReceiver, meetSon, meetMs, "peeled")', wait)
        self.assertIn('LogSeekMeetLost(ai, meetSon, "peeled", WorldTimer::getMSTime() - meetMs, sonVictim,', wait)
        self.assertLess(wait.index("task.valid = false;"), wait.index('"peeled"'))
        self.assertLess(wait.index('"peeled"'), wait.index("task.row = nullptr;"))
        self.assertIn("event=wait,reason=receiver_wait", ACTIONS)
        self.assertIn("g_seekMeetWaitLog[bot->GetObjectGuid().GetRawValue()]", ACTIONS)

    def test_every_terminal_meet_row_sits_behind_the_winning_close(self):
        # Each reader holds its own copy of the meet; only the caller whose compare-and-clear returns true logs the
        # taken / lost / peeled row, so a stale reader cannot log a duplicate or a contradicting end.
        fn = update()
        end = fn[:fn.index("if (WalkoutActive(state))")]
        self.assertIn("cleared = it != g_walkouts.end() &&", end)
        gate = end.index("if (cleared && takenBy)")
        self.assertLess(end.index("CloseWalkoutMeetIf("), gate)
        self.assertLess(gate, end.index('event=taken'))
        self.assertLess(end.index("else if (cleared)"), end.index("LogSeekMeetLost(ai, guid, lost,"))
        self.assertEqual(1, end.count("LogSeekMeetLost("))
        # Every LogSeekMeetLost call in the file is the cleared branch or the receiver's peeled close.
        self.assertEqual(2, ACTIONS.count("LogSeekMeetLost(ai,") - 0)
        pick = tank_pick()
        self.assertIn('if (SeekMeetClose(bot->GetInstanceId(), meetReceiver, meetSon, meetMs, "peeled"))\n'
                      "                    LogSeekMeetLost(", pick)
        helper = body(ACTIONS, "bool SeekMeetClose(")
        self.assertIn("CloseWalkoutMeetIf(it->second.state, receiver, son, meetMs)", helper)
        # A won close records a counted loss for the nearest take's reach (walk 57 F4); a lost race records nothing.
        self.assertLess(helper.index("CloseWalkoutMeetIf("), helper.index("WalkoutNoteMeetLost("))
        self.assertIn("WalkoutNoteMeetLost(it->second.state.lostMeets, reason, son, WorldTimer::getMSTime(),"
                      " kSeekNearestTakeMeetLostMs);", helper)
        self.assertIn("if (cleared && !takenBy)\n                    WalkoutNoteMeetLost(it->second.state.lostMeets, lost,"
                      " state.meetSon, nowMs,", end)
        # The policy compares the receiver, the Son and the shot time before it clears.
        policy = inline_body(POLICY, "inline bool WalkoutMeetCloseIf(")
        for term in ("s.meetReceiver != receiver", "s.meetSon != son", "s.meetMs != meetMs"):
            self.assertIn(term, policy)

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
        self.assertIn("CloseWalkoutMeetIf(it->second.state, state.meetReceiver, state.meetSon, state.meetMs);", meet)

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
        self.assertIn("seek take: %s -> %u victim=%s reason=%s meet=%d src=%s", ACTIONS)
        take = execute()
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        self.assertIn("const bool canTaunt = taunt && ai->CanCastSpell(taunt, task.source);", take)
        self.assertIn("gateRefused ? \"taunt_gated\" : SeekTakeReasonWord(chaseRefused, taunt != nullptr, canTaunt, edgeFar),",
                      take)
        self.assertIn("task.meet, SeekTakeSrcWord(task.takeSrc, edgeTake));", take)
        self.assertLess(take.index("const bool canTaunt ="), take.index("LogSeekTake("))
        # The taunt gate is read BEFORE the row is written (the row used to say `taunt` for a refused cast), once,
        # and the same verdict ends the take.
        self.assertLess(take.index("const bool gateRefused ="), take.index("LogSeekTake("))
        self.assertEqual(take.count("TauntRefusedByGate("), 1)
        self.assertIn("if (!canTaunt || gateRefused)", take)
        self.assertIn("bool meet = false;", ACTIONS_H)

    def test_the_pre_place_arrival_diagnostics(self):
        # Walk 57 F3: one `event=preplace_arrive` row per pre-place (claimed under the lock), and the meet row's
        # `receiver_point_yd=`, both always on with the pre-place (no late key in the way).
        task = body(ACTIONS, "bool SeekMeetPrePlaceTask(Player* bot, const BossTacticRow* row, SeekTankTask& task)")
        self.assertIn('"son=%u,event=preplace_arrive,receiver=%s,after_ms=%u,point_yd=%.1f"', task)
        self.assertIn("if (!walk && botAi && SeekMeetPrePlaceNoteArrive(instanceId, self, progress.gen, nowMs, afterMs))",
                      task)
        self.assertIn("LogSeekMeet(botAi, info1);", task)
        arrive = task[task.index("if (!walk && botAi"):]
        self.assertNotIn("dungeonRunSeekMeetPrePlaceLate", arrive)
        claim = body(ACTIONS, "bool SeekMeetPrePlaceNoteArrive(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", claim)
        self.assertIn("WalkoutPrePlaceNoteArrive(it->second.state, bot, gen, nowMs, afterMs)", claim)
        fn = update()
        meet = fn[fn.index("Player* const receiver = meetRoles.tanks[static_cast<std::size_t>(pick)];"):]
        self.assertIn("? receiver->GetDistance2d(it->second.state.prePlaceX, it->second.state.prePlaceY)", meet)
        self.assertLess(meet.index("float const receiverPointYd ="), meet.index('"receiver_point_yd=%.1f"'))
        self.assertIn("receiverPointYd);", meet)
        # Read before the shot's hand-over can clear the pre-place.
        self.assertLess(meet.index("float const receiverPointYd ="), meet.index("WalkoutPrePlaceNoteMeet(it->second.state);"))

    def test_the_meet_rows(self):
        for row in ('"son=%u,event=meet,receiver=%s,tank_idx=%d,receiver_yd=%.1f,due_in=%u,"\n'
                    '                                 "receiver_point_yd=%.1f"',
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
        self.assertIn("CloseWalkoutMeetIf(it->second.state, state.meetReceiver, state.meetSon, state.meetMs);", fn)
        self.assertIn('"son=%u,event=lost,reason=%s,after_ms=%u"', ACTIONS)
        self.assertNotIn("meetTakenLogged", ACTIONS)


class MeetHoldWiring(unittest.TestCase):
    """Walk 54, fix A: the meet Son stays held until a tank has it (tools/test_seek_aura_policy.cpp pins the rule)."""

    def read(self):
        return body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")

    def test_the_meet_is_read_under_the_flag_and_the_seek_pull_row(self):
        read = self.read()
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunSeekSonMeet && SeekPullRulesOn(bot->GetMapId(), row))", read)
        self.assertIn("SeekMeetOpen(meetReceiver, meetMs, nowMs))", read)
        self.assertIn("out.meetOpen = true;", read)
        self.assertIn("SeekMeetWalkers(bot->GetInstanceId(), meetPuller, meetReturner);", read)

    def test_the_puller_and_returner_do_not_count_on_the_non_tank_clocks(self):
        read = self.read()
        loop = read[read.index("seenSons.reserve(fightSons.size());"):read.index("Creature* latched =")]
        self.assertIn("if (out.meetOpen && seen.guid == out.meetSon && victim &&", loop)
        self.assertIn("SeekMeetWalkerHasSon(victim->GetObjectGuid().GetRawValue(), meetPuller, meetReturner))", loop)
        clear = loop[loop.index("SeekMeetWalkerHasSon("):]
        self.assertIn("seen.victimTank = false;", clear)
        self.assertIn("seen.victimNonTank = false;", clear)
        # The exclusion follows the role read, so it overrides it.
        self.assertLess(loop.index("seen.victimNonTank = victim && !seen.victimTank;"),
                        loop.index("SeekMeetWalkerHasSon("))
        # The clock reads seenSons after that.
        self.assertLess(read.index("SeekMeetWalkerHasSon("), read.index("SeekHealerClockTick(record.healerHeldSince"))

    def test_no_forced_burn_while_the_meet_son_is_off_a_tank(self):
        read = self.read()
        self.assertIn("if (out.meetOpen && pickedSon && pickedSon->GetObjectGuid().GetRawValue() == out.meetSon)", read)
        self.assertIn("meetKeepsSon = !(pickedVictim && PlayerbotAI::IsTank(pickedVictim));", read)
        self.assertIn("if (SeekLateEngagedBurnWithin(pickedSon && pickedSon->IsInCombat(), dueMs, out.dueInMs, lateLeadMs) &&\n"
                      "        !meetKeepsSon)\n        out.forceBurn = true;", read)

    def test_the_hold_checks_the_meet_first(self):
        unit = body(ACTIONS, "bool SeekMeetHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("return SeekMeetHoldsSon(state.meetOpen, unit->GetObjectGuid().GetRawValue() == state.meetSon,", unit)
        self.assertIn("victim && PlayerbotAI::IsTank(victim));", unit)
        holds = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("if (SeekMeetHoldsUnit(state, unit))\n        return true;", holds)
        self.assertLess(holds.index("SeekMeetHoldsUnit(state, unit)"), holds.index("SeekHoldDecision("))
        why = body(ACTIONS, "SeekHoldWhy SeekHoldWhyOfUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("if (SeekMeetHoldsUnit(state, unit))\n        return SeekHoldWhy::Held;", why)
        self.assertLess(why.index("SeekMeetHoldsUnit(state, unit)"), why.index("SeekHoldWhyOf("))

    def test_the_policy_rule(self):
        policy = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
        self.assertIn("return meetOpen && sonIsMeetSon && !sonVictimIsTank;", policy)


class TakeEdgeWiring(unittest.TestCase):
    """Walk 54, fix C: the take edge (tools/test_seek_walkout_policy.cpp pins the rule)."""

    def test_the_flag_is_read_as_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekTakeEdge", false)', CONFIG)
        self.assertIn("dungeonRunSeekTakeEdge = config.GetBoolDefault", CONFIG)
        self.assertIn("bool dungeonRunSeekTakeEdge = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekTakeEdge = 0", TEMPLATE)

    def test_the_take_lets_a_son_at_the_edge_through_only_with_the_flag(self):
        take = execute()
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        base = take.index("bool chaseRefused = BossAnchorRefusesChase(ai, bot, task.source, true);")
        edge = take.index("if (chaseRefused && sPlayerbotAIConfig.dungeonRunSeekTakeEdge)")
        self.assertLess(base, edge)
        self.assertLess(edge, take.index("const bool canTaunt"))
        block = take[edge:take.index("const bool canTaunt")]
        self.assertIn("SeekTakeEdgeCheck(AnchorCircleOf(*anchorRow, false),", block)
        self.assertIn("chaseRefused = edge != SeekTakeEdge::Allowed;", block)
        self.assertIn("edgeFar = edge == SeekTakeEdge::Far;", block)
        # The raid circle (not the tanks'), and the victim's position, never the Son's, is the second point.
        self.assertIn("sonVictim ? sonVictim->GetPositionX() : 0.0f", block)
        self.assertIn("sonVictim != nullptr", block)
        # The Attack() path is reached only after the bounded check: it needs !chaseRefused, which the check set, and
        # no Attack() call sits anywhere before the check in the take.
        self.assertNotIn("Attack(bot,", take[:edge])
        after = take[take.index("const bool canTaunt"):]
        self.assertIn("if (!chaseRefused && AI_VALUE(Unit*, \"current target\") != task.source)\n        {\n"
                      "            const bool attacked = Attack(bot, task.source);", after)
        self.assertIn("if (attacked && !canTaunt)", after)
        self.assertEqual(1, take.count("Attack(bot, task.source)"))
        self.assertLess(take.index("edgeFar = edge == SeekTakeEdge::Far;"), take.index("Attack(bot, task.source)"))

    def test_the_bounds_are_the_policys(self):
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertIn("constexpr float kSeekTakeEdgeMaxPastYd = 8.0f;", policy)
        self.assertIn("constexpr float kSeekTakeEdgeOnVictimYd = 10.0f;", policy)
        self.assertIn("sonFromCentre <= raid.radius + kSeekTakeEdgeMaxPastYd", policy)
        self.assertIn("<= kSeekTakeEdgeOnVictimYd", policy)


class PeeledRowWiring(unittest.TestCase):
    def test_the_peeled_row_names_the_victim(self):
        self.assertIn('",victim=%u,victim_role=%s,son_yd=%.1f"', ACTIONS)
        fn = body(ACTIONS, "void LogSeekMeetLost(PlayerbotAI* ai, std::uint64_t guid, const char* reason,")
        self.assertIn('PlayerbotAI::IsHeal(player) ? "healer" : "dps"', fn)
        self.assertIn('"tank"', fn)
        self.assertIn("peeler->GetGUIDLow(),\n                 role, sonYd);", fn)
        for word in ("`victim=<guid low>`", "`victim_role=", "`son_yd="):
            self.assertIn(word, DOC)


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
                     "timeout", "chase_refused", "edge_far", "no_taunt", "attack", "taunt", "taunt_gated", "peeled", "receiver_charmed",
                     "receiver_victim", "receiver_busy", "receiver_wait"):
            self.assertIn("`%s`" % word, DOC, word)


if __name__ == "__main__":
    unittest.main()
