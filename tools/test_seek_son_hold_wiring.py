"""Wiring pins for the Son hold on a non-tank (Hakkar, walk 45: a Son brought onto the platform sat on non-tanks and
the seek hold kept it alive; 87 of 99 deaths from Sons were non-tanks).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (SeekHoldDecision's non-tank time,
SeekTakeFightingSonOnHealer), tools/test_seek_fight_state_policy.cpp (SeekNonTankHeldMs, SeekHealerTakeGuid) and
tools/test_seek_walkout_policy.cpp (the siphon lead of the walk-out start). These pins prove the live code feeds them.
Run: python3 -m unittest tools/test_seek_son_hold_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
SEEK_POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
STATE_POLICY = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def guard_body(text, signature):
    # The function's definition (the forward declaration comes first in the file).
    start = text.index(signature, text.index(signature) + len(signature))
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekSonHoldWiring(unittest.TestCase):
    def test_the_hold_reads_the_sons_non_tank_time(self):
        hold = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekNonTankHeldMs(state.nonTankSince, unit->GetObjectGuid().GetRawValue(), state.nowMs)", hold)
        why = body(ACTIONS, "SeekHoldWhy SeekHoldWhyOfUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekNonTankHeldMs(state.nonTankSince, unit->GetObjectGuid().GetRawValue(), state.nowMs)", why)
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("out.nonTankSince = record.healerHeldSince;", read)
        self.assertLess(read.index("SeekHealerClockTick("), read.index("out.nonTankSince = record.healerHeldSince;"))

    def test_the_first_off_tank_takes_after_8_s_whatever_the_third_does(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekHealerTakeGuid(record.healerHeldSince, nowMs)", read)
        self.assertNotIn("thirdTankTakes", read)

    def test_the_take_reaches_the_raids_circle_and_logs(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        # A Son on a non-tank stands where the raid stands: inside the everyone circle (30 yd), past the tanks' (25).
        self.assertIn("BossAnchorRefusesChase(ai, bot, task.source, true)", take)
        self.assertIn("LogSeekTake(ai, bot, task.source)", take)
        log = body(ACTIONS, "void LogSeekTake(PlayerbotAI* ai, Player* bot, Unit* son)")
        self.assertIn('"seek take: %s -> %u victim=%s"', log)
        refuse = body(ACTIONS, "bool ai::BossAnchorRefusesChase(PlayerbotAI* ai, Player* bot, Unit* unit, bool raidCircle)")
        self.assertIn("AnchorCircleOf(*row, false)", refuse)

    def test_the_walk_out_waits_for_the_siphon(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutMayStartFor(record.state, cycleNo, sonGuid, self, nowMs, dueInMs, rules)", update)
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, out.dueInMs, walkoutWanted, out)", read)


class SeekSonHoldReview(unittest.TestCase):
    def test_the_boss_victim_takes_no_son(self):
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        gate = tank.index("SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot)")
        self.assertLess(tank.index("ReadSeekFightCached(ai, bot, state)"), gate)
        self.assertLess(gate, tank.index("if (onTank)"))

    def test_no_comment_keeps_the_three_tank_gate(self):
        for stale in ("once the third off-tank cannot take it", "while the third off-tank cannot",
                      "Nothing\n        // here when the third off-tank can still take it"):
            self.assertNotIn(stale, ACTIONS)


class SeekSonHoldLastRound(unittest.TestCase):
    def test_the_fallback_pulls_while_the_third_tank_is_the_victim(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekPullActorsFor(roles.tanks.size(), thirdTankIsBossVictim, anchorShot)", read)
        self.assertIn("thirdTankIsBossVictim = roles.tanks.size() >= 3 && boss->GetVictim() == roles.tanks[2]", read)
        self.assertIn("FindSeekFallbackPuller(ai, bot, roles, boss, *row, actors.fallback)", read)
        self.assertIn("out.fallbackOpen = actors.fallback;", read)
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("FindSeekFallbackPuller(ai, bot, roles, state.boss, *state.row, state.fallbackOpen)", tank)
        self.assertIn("(roles.tanks.size() >= 3 && !state.fallbackOpen)", tank)


class SeekHoldClockEngagement(unittest.TestCase):
    def test_the_hold_clock_counts_engaged_time_only(self):
        # Every Son in the fight list used to feed the clock, and the hold rule holds any non-selected Son wherever it
        # stands: after 20 s every idle spare around the spot was released for good and the area veto stopped
        # protecting the reservoir. The clock now takes a Son only while it fights a victim, and the cap reaches the
        # selected source and a Son on a tank inside the window.
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn("SeekHoldClockTicks(son == out.source, engaged, victimTank, out.windowOpen)", tick)
        # The alive/feed composition is the policy's (SeekSonTicks), not a hand-rolled expression in the action.
        self.assertIn("const bool alive = IsSeekSource(out, son);", tick)
        self.assertIn("SeekSonTicks(alive, SeekHoldClockTicks(son == out.source, engaged, victimTank, out.windowOpen),",
                      tick)
        self.assertIn("SeekHoldsUnit(out, son))", tick)
        self.assertIn("SeekHoldSeen{guid, alive, ticks}", tick)
        self.assertNotIn("capped ||", tick)
        self.assertIn("sServerFacade.IsAlive(son)", tick)
        self.assertIn("son->IsInCombat()", tick)
        self.assertIn("son->GetVictim()", tick)
        self.assertLess(tick.index("SeekHoldClockTicks("), tick.index("SeekHoldsUnit(out, son)"))
        self.assertIn("inline bool SeekHoldClockTicks(bool selected, bool engaged, bool victimTank, bool windowOpen)",
                      SEEK_POLICY)
        self.assertIn("return engaged && (selected || (victimTank && windowOpen));", SEEK_POLICY)

    def test_the_hold_clock_drops_what_nobody_ticks(self):
        # A read only reaches the Sons around the bot, so an unseen Son keeps its entry; the sweep drops a below-cap
        # record no read ticked for kSeekHoldUntickedMs and a capped record no read saw alive for kSeekHoldStaleMs, so
        # the map cannot grow and a respawn under the same guid starts a clean hold.
        self.assertIn("constexpr std::uint32_t kSeekHoldUntickedMs = 5000;", STATE_POLICY)
        self.assertIn("constexpr std::uint32_t kSeekHoldStaleMs = 20000;", STATE_POLICY)
        clock = body(STATE_POLICY, "inline void SeekHoldClockTick(")
        self.assertIn("kSeekHoldUntickedMs", clock)
        self.assertIn("kSeekHoldStaleMs", clock)
        self.assertIn("SeekAgeMs(", clock)
        # A read that does not tick a live Son never erases it: only the seen-dead branch erases inside the read.
        self.assertNotIn("if (!son.held)", clock)


class SeekSonRowsWiring(unittest.TestCase):
    def test_the_four_rows_carry_their_formats(self):
        self.assertIn('"son=%u,event=%s,reason=%s,held_ms=%u,nontank_ms=%u,hp=%u,spot_yd=%.1f,cycle=%u,due_in=%u,'
                      'victim=%s"', ACTIONS)
        self.assertIn('"son=%u,cycle=%u,due_in=%u,since_siphon=%d,phase=%s,raid_alive=%d,raid_in_cloud=%d,'
                      'held_ms=%u,"', ACTIONS)
        self.assertIn('"last_reason=%s"', ACTIONS)
        self.assertIn('"BossTacticsSonHold"', ACTIONS)
        self.assertIn('"BossTacticsSonDeath"', ACTIONS)
        self.assertEqual(ACTIONS.count('"held=%u:%u,hp=%u,held_ms=%u"'), 2)
        self.assertIn('"hakkar_dmg=%u,hakkar_hp=%u,hp_seen=%u"', ACTIONS)

    def test_only_hakkars_seek_row_logs_them(self):
        hold = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', hold)
        self.assertIn("out.row->targetEntry == kSonOfHakkarEntry", hold)
        self.assertIn("const std::uint32_t kSonOfHakkarEntry = 11357;", ACTIONS)
        self.assertIn("const float kPoisonCloudYd = 8.0f;", ACTIONS)

    def test_log_event_runs_outside_the_seek_lock(self):
        call = ACTIONS.index("TickSeekHoldClock(bot, out, fightSons, record, nowMs, seekRows);")
        lock = ACTIONS.rindex("std::lock_guard<std::mutex> lock(g_seekMutex);", 0, call)
        start = ACTIONS.rindex("{", 0, lock)
        depth = 0
        i = start
        while i < len(ACTIONS):
            if ACTIONS[i] == "{":
                depth += 1
            elif ACTIONS[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = ACTIONS[start:i + 1]
        self.assertIn("TickSeekHoldClock(", block)
        self.assertNotIn("logEvent", block)
        self.assertLess(i, ACTIONS.index("sPlayerbotAIConfig.logEvent(ai, row.type, row.info1, row.info2);"))
        self.assertNotIn("logEvent", guard_body(ACTIONS, "void TickSeekHoldClock("))


class SeekSonRowReview(unittest.TestCase):
    """The Son rows after the review: a joinable id, one release per hold, deaths for Sons never held, the fight's
    phase and the real last release on the death row, and no death row for a despawn."""

    def test_every_son_id_is_the_guid_low(self):
        # The Son rows join the HoldKill `held=11357:<low>`, `seek take: -> <low>` and walk-out rows by the guid low;
        # a raw guid is a different number.
        ids = list(re.finditer(r'"son=%u[^"]*"', ACTIONS))
        self.assertEqual(len(ids), 2)
        for match in ids:
            call = ACTIONS[ACTIONS.rfind("snprintf(", 0, match.start()):ACTIONS.index(";", match.end())]
            self.assertIn("ObjectGuid(guid).GetCounter()", call)

    def test_one_release_row_per_hold_record(self):
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn("const bool hasHoldRecord = record.holdSince.count(guid) || "
                      "record.healerHeldSince.count(guid);", tick)
        self.assertIn("SeekShouldLogRelease(hasHoldRecord, record.releaseLogged.count(guid) != 0)", tick)
        self.assertIn("record.releaseLogged.insert(guid);", tick)
        self.assertIn("record.lastRelease[guid] = why;", tick)
        self.assertIn("record.releaseLogged.erase(event.guid);", tick)

    def test_deaths_come_from_the_sons_the_fight_has_seen(self):
        # A death is found through the guids an earlier read listed, whether or not the corpse still sits in fightSons.
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn("record.sonsSeen.insert(son->GetObjectGuid().GetRawValue());", tick)
        self.assertIn("SeekDeathCandidates(record.sonsSeen, liveNow, record.sonDeathLogged)", tick)
        self.assertIn("record.sonsSeen.erase(guid);", tick)
        self.assertNotIn("if (son->GetEntry() != kSonOfHakkarEntry || sServerFacade.IsAlive(son)", tick)

    def test_a_corpse_still_in_fightsons_is_a_death(self):
        # A sole-kill candidate keeps a corpse in fightSons for up to a second: liveNow counts only live Sons, so the
        # death row (real held_ms) is pushed before the clock's own end.
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        live = tick[tick.index("std::set<std::uint64_t> liveNow;"):tick.index("SeekDeathCandidates(")]
        self.assertIn("if (sServerFacade.IsAlive(son))", live)
        self.assertIn("liveNow.insert(son->GetObjectGuid().GetRawValue());", live)
        self.assertLess(tick.index("SeekDeathCandidates("), tick.index("SeekHoldClockTick(record.holdSince"))

    def test_gone_keeps_the_record_and_logs_once(self):
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        gone = tick[tick.index("const auto held = record.holdSince.find(guid);"):
                   tick.index("record.sonsSeen.erase(guid);")]
        self.assertNotIn("holdSince.erase", gone)
        self.assertNotIn("healerHeldSince.erase", gone)
        self.assertIn("record.goneLogged.insert(guid);", gone)
        skip = tick[tick.index("record.goneLogged.erase(event.guid) != 0"):]
        skip = skip[:skip.index("continue;")]
        self.assertNotIn("PushSonHoldRow", skip)
        self.assertIn("record.releaseLogged.erase(event.guid);", skip)

    def test_the_release_latch_dies_with_its_record(self):
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        # Every transition drops the latch, so a new hold's start clears it too.
        push = 'event.start ? "tick" : SeekHoldEndWord(reason), event.heldMs, text);'
        self.assertIn(push, tick)
        after = tick[tick.index(push):]
        self.assertIn("record.releaseLogged.erase(event.guid);", after[:after.index("}")])
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("std::set<std::uint64_t> healerBefore;", read)
        self.assertIn("healerBefore.insert(entry.first);", read)
        guard = "if (!record.healerHeldSince.count(guid) && !record.holdSince.count(guid))"
        self.assertIn(guard, read)
        remove = read[read.index(guard):]
        self.assertIn("record.releaseLogged.erase(guid);", remove[:remove.index("\n        }")])

    def test_the_death_row_reads_the_fight_phase_and_last_release(self):
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn("PushSonDeathRow(rows, bot, out, record, nowMs, guid, creature, SeekFightPhaseWord(out));", tick)
        death = body(ACTIONS, "void PushSonDeathRow(")
        self.assertIn("record.lastRelease.find(guid)", death)
        self.assertIn("lastReason = SeekHoldWhyWord(release->second);", death)
        self.assertNotIn("holdVerdict", death)
        phase = body(ACTIONS, "const char* SeekFightPhaseWord(const SeekFight& state)")
        self.assertIn('state.ready || state.forceBurn ? "burn" : "gather"', phase)
        self.assertIn('return state.windowOpen ? "hold" : "none";', phase)

    def test_a_despawn_is_not_a_death(self):
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        loop = tick[tick.index("SeekDeathCandidates("):tick.index("std::vector<SeekHoldEvent> events;")]
        self.assertIn("if (creature && !sServerFacade.IsAlive(creature))", loop)
        self.assertIn("if (creature)", loop)
        gone = loop[loop.index("const auto held = record.holdSince.find(guid);"):]
        self.assertNotIn("PushSonDeathRow", gone)
        self.assertIn("SeekHoldEndReason::Gone", gone)


if __name__ == "__main__":
    unittest.main()
