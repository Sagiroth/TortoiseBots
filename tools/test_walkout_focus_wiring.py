"""Wiring pins for the walk-out focus (AiPlayerbot.DungeonRunSeekWalkoutFocus, Hakkar walk 55 F1).

The rules are pure and tested in tools/test_seek_walkout_policy.cpp (WalkoutFocusBlocks, WalkoutPullShotSpell,
WalkoutFaceFirst, WalkoutFocusPullerLive, WalkoutPreemptRowDue). These pins prove the live code calls them: the
facing branch returns before any cast, the multiplier is registered and reads the walk-out record, and the hunter's
pull shot is the focus's pick only with the flag on.
Run: python3 -m unittest tools/test_walkout_focus_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BotStateTriggers.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def walkout_branch():
    fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
    return fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]


class WalkoutFocusWiring(unittest.TestCase):
    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutFocus", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekWalkoutFocus = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekWalkoutFocus = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekWalkoutFocus`", SETTINGS)

    def test_the_facing_branch_returns_before_the_cast(self):
        walk = walkout_branch()
        face = walk.index("if (step.returnAfterFace)\n                {\n"
                          "                    LogSeekWalkoutStand(ai, bot, task.source, WalkoutStandBranch::Face, range);\n"
                          "                    return true;")
        stop = walk.index("if (step.stop && !bot->IsStopped())\n                    ai->StopMoving();")
        turn = walk.index("if (step.face)\n                    sServerFacade.SetFacingTo(bot, task.source, step.force);")
        # Stop, then turn, then return: the stop's spline cannot undo the facing.
        self.assertLess(stop, turn)
        self.assertLess(turn, face)
        self.assertLess(face, walk.index("WalkoutCurrentCast(bot, task.source"))
        self.assertLess(face, walk.index("return ai->CastSpell(spell, task.source);") if "return ai->CastSpell(spell, task.source);" in walk
                        else walk.index("const bool pullCast = ai->CastSpell(spell, task.source);"))
        self.assertLess(face, walk.index("bot->InterruptNonMeleeSpells(false);"))
        self.assertIn("const WalkoutFaceStep step = WalkoutFaceThenCast(sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast,\n"
                      "                                                                 focusOn, rulesOn, inFront);", walk)
        # The old split is gone from the glue: WalkoutFaceThenCast is the one reader of WalkoutFaceFirst.
        self.assertNotIn("WalkoutFaceFirst(", walk)
        self.assertIn("const bool focusOn = sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus;", walk)

    def test_a_pull_shot_in_flight_is_waited_for(self):
        walk = walkout_branch()
        wait = walk.index("if (focusOn && WalkoutPullShotWaits(task.walkoutPullCastMs, WorldTimer::getMSTime(),")
        self.assertLess(wait, walk.index("bot->InterruptNonMeleeSpells(false);"))
        self.assertLess(wait, walk.index("AbortSeekWalkoutUnspent(ai, bot, sonYd, task.walkoutGen);"))
        self.assertIn("task.source->IsInCombat()))\n                {\n"
                      "                    LogSeekWalkoutStand(ai, bot, task.source, WalkoutStandBranch::Wait, range);\n"
                      "                    return true;", walk)
        self.assertIn("if (pullCast && focusOn)\n                    NoteSeekWalkoutPullCast(bot, task.walkoutGen);", walk)
        note = body(ACTIONS, "void NoteSeekWalkoutPullCast(Player* bot, std::uint32_t gen)")
        self.assertIn("it->second.state.pullCastMs = WorldTimer::getMSTime();", note)
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", note)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (rules.shotStall && WalkoutNoShotAbortDue(stallDue, state.pullCastMs, nowMs))",
                      update)
        self.assertIn("out.walkoutPullCastMs = state.pullCastMs;", update)
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        self.assertIn("task.walkoutPullCastMs = state.walkoutPullCastMs;", find)

    def test_the_hunter_pull_shot_is_the_focus_pick(self):
        walk = walkout_branch()
        self.assertIn('ai->CanCastSpell("arcane shot", task.source)', walk)
        self.assertIn("? WalkoutPullShotSpell(bot->GetClass(), bot->GetClass() == CLASS_HUNTER &&", walk)
        # Flag off: today's shot.
        self.assertIn(": WalkoutShotSpell(bot->GetClass());", walk)
        self.assertNotIn("serpent sting", walk)
        # The cast-fail abort still covers the picked shot (a warrior or paladin has none).
        self.assertIn("if (!*spell || !ai->CanCastSpell(spell, task.source))", walk)

    def test_the_multiplier_is_registered(self):
        self.assertIn("class BossTacticsWalkoutFocusMultiplier : public Multiplier", ACTIONS_H)
        self.assertIn('Multiplier(ai, "boss tactics walkout focus")', ACTIONS_H)
        combat = body(STRATEGY, "void BossTacticsStrategy::InitCombatMultipliers(")
        self.assertIn("multipliers.push_back(new BossTacticsWalkoutFocusMultiplier(ai));", combat)
        self.assertLess(combat.index("BossTacticsSeekPetHoldMultiplier"), combat.index("BossTacticsWalkoutFocusMultiplier"))

    def test_the_multiplier_reads_the_record_and_the_policy(self):
        fn = body(ACTIONS, "float BossTacticsWalkoutFocusMultiplier::GetValue(Action* action)")
        self.assertIn("!sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus", fn)
        # The inputs go in by name, so a swap shows in review; the decision is the pure WalkoutFocusHolds.
        self.assertIn("in.alive = sServerFacade.IsAlive(bot);", fn)
        self.assertIn("in.charmed = !bot->GetCharmerGuid().IsEmpty();", fn)
        self.assertIn("in.hpPct = bot->GetHealthPercent();", fn)
        self.assertIn("if (!WalkoutFocusHolds(in, name))\n        return 1.0f;", fn)
        self.assertIn("return 0.0f;", fn)
        self.assertIn('"seek pull: walk-out preempted action=%s"', fn)
        read = body(ACTIONS, "bool SeekWalkoutFocusRead(Player* bot, std::uint32_t nowMs, WalkoutFocusInput& in,")
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", read)
        self.assertIn("WalkoutPullerRecordIn(g_walkouts, bot->GetInstanceId(), bot->GetObjectGuid().GetRawValue());", read)
        self.assertIn("shot = record->state.shot;", read)
        self.assertIn("in.recordedPuller = true;", read)
        self.assertIn("in.sinceStartMs = nowMs - startMs;", read)
        self.assertIn("if (WalkoutFocusHolds(in, action) && WalkoutPreemptRowDue(record->preemptLogStartMs, startMs))", read)
        self.assertIn("record->preemptLogStartMs = startMs;", read)

    def test_no_lock_off_the_seek_map_or_without_a_running_task(self):
        fn = body(ACTIONS, "float BossTacticsWalkoutFocusMultiplier::GetValue(Action* action)")
        early = fn.index("if (WalkoutFocusAllows(name) || !GetBossTactics().HasSeekAuraMap(bot->GetMapId()))\n        return 1.0f;")
        self.assertLess(early, fn.index("SeekWalkoutFocusRead("))
        read = body(ACTIONS, "bool SeekWalkoutFocusRead(Player* bot, std::uint32_t nowMs, WalkoutFocusInput& in,")
        # The fast path is the running count (tasks only), not the busy count (which also covers walk backs).
        self.assertIn("if (g_walkoutsRunning.load(std::memory_order_relaxed) == 0)\n        return false;", read)
        self.assertNotIn("g_walkoutsActive", read)
        self.assertLess(read.index("g_walkoutsRunning.load"), read.index("std::lock_guard"))
        self.assertIn("std::atomic<int> g_walkoutsRunning{0};", ACTIONS)
        note = body(ACTIONS, "void NoteWalkoutBusy(const WalkoutBusyMarks& before, const SeekWalkoutState& after)")
        self.assertIn("g_walkoutsRunning.fetch_add(WalkoutCountStep(before.running, now.running), std::memory_order_relaxed);", note)
        self.assertIn("g_walkoutsActive.fetch_add(WalkoutCountStep(before.busy, now.busy), std::memory_order_relaxed);", note)
        # Every record change goes through the marks, and no busy-only snapshot is left.
        self.assertNotIn("bool const before = WalkoutBusy(", ACTIONS)
        self.assertIn("NoteWalkoutBusy(WalkoutMarksOf(it->second.state), SeekWalkoutState());", ACTIONS)

    def test_reaction_actions_still_run(self):
        # Lead decision (walk 56 review): combat actions are held; reaction actions still run.
        reaction = body(STRATEGY, "void BossTacticsStrategy::InitReactionMultipliers(")
        self.assertNotIn("BossTacticsWalkoutFocusMultiplier", reaction)
        noncombat = body(STRATEGY, "void BossTacticsStrategy::InitNonCombatMultipliers(")
        self.assertNotIn("BossTacticsWalkoutFocusMultiplier", noncombat)


class WalkoutHoldCombatWiring(unittest.TestCase):
    """DungeonRunSeekWalkoutHoldCombat (walk 56 fix 1): the live unshot puller stays on the combat engine."""

    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutHoldCombat", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekWalkoutHoldCombat = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekWalkoutHoldCombat = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekWalkoutHoldCombat`", SETTINGS)

    def test_combat_end_reads_the_live_puller_after_the_pull_check(self):
        fn = body(TRIGGERS, "bool CombatEndTrigger::IsActive()")
        hold = fn.index("if (ai::WalkoutHoldCombatOn(sPlayerbotAIConfig.dungeonRunSeekWalkoutHoldCombat,\n"
                        "                                    sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus) &&\n"
                        "            ai::IsLiveWalkoutPuller(bot))\n"
                        "            return false;")
        # After the pull check, before every other veto and before the stale-order clear (which would AttackStop).
        self.assertLess(fn.index("strategy->HasTarget()"), hold)
        self.assertLess(hold, fn.index("CombatOrderHolds("))
        self.assertLess(hold, fn.index("bot->AttackStop();"))
        # Flag first: with it off no record is read.
        self.assertIn('#include "playerbot/strategy/actions/BossTacticsActions.h"', TRIGGERS)
        self.assertIn("bool IsLiveWalkoutPuller(Player* bot);", ACTIONS_H)

    def test_the_live_puller_read_is_the_record_match_and_the_pure_rule(self):
        fn = body(ACTIONS, "bool ai::IsLiveWalkoutPuller(Player* bot)")
        fast = fn.index("g_walkoutsRunning.load(std::memory_order_relaxed) == 0")
        seek = fn.index("!GetBossTactics().HasSeekAuraMap(bot->GetMapId())")
        read = fn.index("WalkoutPullerRecordIn(g_walkouts, bot->GetInstanceId(), bot->GetObjectGuid().GetRawValue());")
        self.assertLess(fast, seek)
        self.assertLess(seek, read)
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", fn)
        self.assertIn("bossGuid = record->bossGuid;", fn)
        # The boss: alive, in combat, not evading (review L2).
        self.assertIn("bot->GetMap()->GetCreature(ObjectGuid(bossGuid))", fn)
        self.assertIn("bool const bossFighting = WalkoutBossFighting(boss != nullptr, boss && sServerFacade.IsAlive(boss),\n"
                      "                                                  boss && boss->IsInCombat(), boss && boss->IsInEvadeMode());", fn)
        self.assertIn("return WalkoutHoldsCombat(true, true, sServerFacade.IsAlive(bot), !bot->GetCharmerGuid().IsEmpty(), sinceStartMs,\n"
                      "                              shot, bossFighting);", fn)
        # The boss lookup runs after the lock is released.
        self.assertLess(fn.index("bossGuid = record->bossGuid;"), fn.index("GetCreature(ObjectGuid(bossGuid))"))
        # The focus read shares the same record match (another instance's record never matches; behaviour-tested in
        # TestWalkoutPullerRecordPerInstance).
        focus = body(ACTIONS, "bool SeekWalkoutFocusRead(Player* bot, std::uint32_t nowMs, WalkoutFocusInput& in,")
        self.assertIn("WalkoutPullerRecordIn(g_walkouts, bot->GetInstanceId(), bot->GetObjectGuid().GetRawValue());", focus)

    def test_the_pick_reads_the_combat_engine_under_the_flag(self):
        fn = body(ACTIONS, "Player* FindSeekWalkoutPuller(")
        gate = fn.index("if (WalkoutHoldCombatOn(sPlayerbotAIConfig.dungeonRunSeekWalkoutHoldCombat,\n"
                        "                                sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus))\n"
                        "            c.combatEngine = WalkoutCombatEngineReady(memberAi->IsStateActive(BotState::BOT_STATE_COMBAT),")
        self.assertIn('memberAi->HasAura("food", member) || memberAi->HasAura("drink", member));', fn)
        self.assertNotIn("BOT_STATE_NON_COMBAT", fn)
        # Filled before the first eligibility test, so the costly questions skip it.
        self.assertLess(gate, fn.index("if (c.canShoot && WalkoutEligible(c))"))
        # The bot's own engine, never its pet's combat.
        self.assertNotIn("GetPet()", fn[gate:fn.index("if (c.canShoot && WalkoutEligible(c))")])



class WalkoutFaceCastWiring(unittest.TestCase):
    """DungeonRunSeekWalkoutFaceCast (walk 56 fix 2): turn and cast in one tick; UpdateFaceTarget yields."""

    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutFaceCast", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekWalkoutFaceCast = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekWalkoutFaceCast = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekWalkoutFaceCast`", SETTINGS)

    def test_the_turn_is_forced_and_the_cast_follows(self):
        walk = walkout_branch()
        turn = walk.index("sServerFacade.SetFacingTo(bot, task.source, step.force);")
        # One SetFacingTo in the stand branch, and it carries the step's force.
        self.assertEqual(walk.count("sServerFacade.SetFacingTo(bot, task.source"), 1)
        self.assertLess(turn, walk.index("bot->InterruptNonMeleeSpells(false);"))
        self.assertLess(turn, walk.index("const bool pullCast = ai->CastSpell(spell, task.source);"))

    def test_update_face_target_yields_for_the_live_puller(self):
        fn = body(PLAYERBOT_AI, "void PlayerbotAI::UpdateFaceTarget(uint32 elapsed, bool minimal)")
        self.assertIn("bool const faceCastOn = sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast;", fn)
        gate = fn.index("if (pullTarget == nullptr &&\n"
                        "                        !ai::WalkoutFaceTargetYields(faceCastOn, faceCastOn && ai::IsLiveWalkoutPuller(bot)))")
        # The yield guards the turn itself; every other bot's facing is unchanged.
        self.assertLess(gate, fn.index("sServerFacade.SetFacingTo(bot, target);"))
        self.assertLess(gate, fn.index("bot->SetInFront(target);"))
        self.assertIn('#include "playerbot/SeekWalkoutPolicy.h"', PLAYERBOT_AI)
        # The delay reset still runs on every path.
        self.assertIn("faceTargetUpdateDelay = minimal ? sPlayerbotAIConfig.reactDelay * 10 : sPlayerbotAIConfig.reactDelay * 5;", fn)



class WalkoutInShotClockWiring(unittest.TestCase):
    """DungeonRunSeekWalkoutFaceCast (walk 56 fix 3): the no-shot clock counts only the time in shot position."""

    def test_the_stand_branch_notes_the_shot_position_under_the_flag(self):
        walk = walkout_branch()
        note = walk.index("if (sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast && SeekPullRulesOn(bot->GetMapId(), task.row))\n"
                          "                NoteSeekWalkoutInShot(bot, WalkoutInShotPosition(sonYd <= range, bot->IsWithinLOSInMap(task.source),")
        self.assertIn("bot->GetPositionZ() >= kSeekWalkoutMinStandZ,\n"
                      "                                                                 sServerFacade.isMoving(bot)),\n"
                      "                                      task.walkoutGen);", walk)
        # Read on every outbound tick, before the in-shot branch can return.
        self.assertLess(note, walk.index("const bool inFront = sServerFacade.isInFront("))
        fn = body(ACTIONS, "void NoteSeekWalkoutInShot(Player* bot, bool inShot, std::uint32_t gen)")
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", fn)
        self.assertIn("!WalkoutTaskIs(it->second.state, bot->GetObjectGuid().GetRawValue(), gen)", fn)
        self.assertIn("WalkoutNoteInShot(it->second.state, inShot, WorldTimer::getMSTime());", fn)

    def test_the_stall_reads_the_in_shot_clock_under_the_flag(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("bool const stallDue = WalkoutStallDue(state, nowMs, sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast) &&",
                      update)
        self.assertNotIn("WalkoutShotStalled(", update)



class WalkoutStandLogWiring(unittest.TestCase):
    """DungeonRunSeekWalkoutStandLog (walk 56 fix 4): diagnostic stand and castrefused rows, no behaviour change."""

    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutStandLog", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekWalkoutStandLog = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekWalkoutStandLog = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekWalkoutStandLog`", SETTINGS)

    def test_the_stand_row_is_a_throttled_no_op_with_the_flag_off(self):
        fn = body(ACTIONS, "void LogSeekWalkoutStand(PlayerbotAI* ai, Player* bot, Unit* son, WalkoutStandBranch branch,")
        off = fn.index("if (!sPlayerbotAIConfig.dungeonRunSeekWalkoutStandLog || !ai || !bot || !son)\n        return;")
        claim = fn.index("if (!ClaimSeekWalkoutStandRow(bot, tickMs))\n        return;")
        self.assertLess(off, claim)
        self.assertLess(claim, fn.index("IsWithinLOSInMap(son)"))
        self.assertIn("ai->IsStateActive(BotState::BOT_STATE_COMBAT)", fn)
        claimFn = body(ACTIONS, "bool ClaimSeekWalkoutStandRow(Player* bot, std::uint32_t& tickMs)")
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", claimFn)
        self.assertIn("WalkoutStandRowClaim(record.standLogStartMs, startMs, record.standRowMs, nowMs)", claimFn)
        self.assertIn("tickMs = fresh ? 0 : nowMs - record.standReadMs;", claimFn)
        refused = body(ACTIONS, "bool ClaimSeekWalkoutCastRefusedRow(Player* bot)")
        self.assertIn("WalkoutCastRefusedRowDue(record.refusedLogStartMs, startMs, record.refusedRows)", refused)

    def test_every_branch_logs_its_word(self):
        walk = walkout_branch()
        for word in ("Wait", "Face", "CastWait", "CheckFail", "Cast", "CastRefused", "InPos", "Out"):
            self.assertIn("WalkoutStandBranch::" + word, walk)
        # The check-fail row goes out before the abort clears the record it is keyed by.
        self.assertLess(walk.index("WalkoutStandBranch::CheckFail"), walk.index("AbortSeekWalkoutUnspent(ai, bot, sonYd, task.walkoutGen);"))

    def test_the_castrefused_row_follows_the_cast_spell_call(self):
        walk = walkout_branch()
        clear = walk.index("before = ReadWalkoutCastBefore(bot, task.source);\n"
                           "                    ai->ClearLastCastCheckFail();")
        cast = walk.index("const bool pullCast = ai->CastSpell(spell, task.source);")
        row = walk.index("if (!pullCast && ClaimSeekWalkoutCastRefusedRow(bot))\n"
                         "                        LogSeekWalkout(ai, WalkoutCastRefusedRowText(spellId, refuse).c_str());")
        self.assertLess(clear, cast)
        self.assertLess(cast, row)
        self.assertLess(row, walk.index("return pullCast;"))
        # Only under the flag: the clear and the spell id lookup never run with it off.
        self.assertIn("const bool standLog = sPlayerbotAIConfig.dungeonRunSeekWalkoutStandLog;", walk)
        self.assertIn('const std::uint32_t spellId = standLog && *spell ? AI_VALUE2(uint32, "spell id", spell) : 0;', walk)
        self.assertIn("if (standLog)\n                {\n                    before = ReadWalkoutCastBefore(", walk)
        self.assertIn('"seek pull: walk-out castrefused spell="', (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text())



class WalkoutAllNewFlagsOff(unittest.TestCase):
    """With HoldCombat, FaceCast and StandLog off, every new path is skipped (the pure off-values are in
    tools/test_seek_walkout_policy.cpp: TestWalkoutFlagsOffKeepToday)."""

    def test_every_live_puller_read_sits_behind_its_flag(self):
        trig = body(TRIGGERS, "bool CombatEndTrigger::IsActive()")
        self.assertEqual(trig.count("ai::IsLiveWalkoutPuller(bot)"), 1)
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus) &&\n            ai::IsLiveWalkoutPuller(bot)", trig)
        face = body(PLAYERBOT_AI, "void PlayerbotAI::UpdateFaceTarget(uint32 elapsed, bool minimal)")
        self.assertEqual(face.count("ai::IsLiveWalkoutPuller(bot)"), 1)
        self.assertIn("faceCastOn && ai::IsLiveWalkoutPuller(bot)", face)
        # No other caller.
        for text in (ACTIONS, PLAYERBOT_AI, TRIGGERS):
            self.assertLessEqual(text.count("IsLiveWalkoutPuller(bot)"), 1)

    def test_the_stand_branch_paths_sit_behind_their_flags(self):
        walk = walkout_branch()
        self.assertEqual(walk.count("NoteSeekWalkoutInShot("), 1)
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast && SeekPullRulesOn(bot->GetMapId(), task.row))\n"
                      "                NoteSeekWalkoutInShot(", walk)
        # Two clears, both under the flag: before the final cast check (review L6) and before CastSpell.
        self.assertEqual(walk.count("ai->ClearLastCastCheckFail();"), 2)
        self.assertIn("if (standLog)\n                    ai->ClearLastCastCheckFail();\n"
                      "                if (!*spell || !ai->CanCastSpell(spell, task.source))", walk)
        self.assertEqual(walk.count("ClaimSeekWalkoutCastRefusedRow(bot)"), 1)
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(")
        self.assertEqual(pick.count("c.combatEngine ="), 1)



class WalkoutStartGenGuards(unittest.TestCase):
    """Every compare-and-mutate of a walk-out task compares the start generation (SeekWalkoutState::startGen)."""

    def test_the_readers_snapshot_guards_compare_the_generation(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        # The no-shot abort, the shot/end write-back and the no-stand re-plan: all three by the whole snapshot.
        self.assertEqual(update.count("!WalkoutSnapshotCurrent(it->second.state, state)"), 3)
        self.assertNotIn("it->second.state.startMs != state.startMs", update)
        self.assertIn("WalkoutReturnCurrent(it->second.state, state)", update)
        self.assertNotIn("it->second.state.returner == state.returner", update)

    def test_the_pullers_own_writes_match_its_task_generation(self):
        for sig in ("void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot, float sonYd, std::uint32_t gen)",
                    "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved, std::uint32_t failReason,",
                    "void NoteSeekWalkoutAtStand(Player* bot, std::uint32_t gen)",
                    "void NoteSeekWalkoutInShot(Player* bot, bool inShot, std::uint32_t gen)",
                    "void NoteSeekWalkoutPullCast(Player* bot, std::uint32_t gen)"):
            fn = body(ACTIONS, sig)
            self.assertIn("WalkoutTaskIs(it->second.state, bot->GetObjectGuid().GetRawValue(), gen)", fn, sig)
            self.assertNotIn("it->second.state.puller != bot->GetObjectGuid().GetRawValue()", fn, sig)
        walk = walkout_branch()
        # 5 writes plus the shot wait's read and end (walk 58 F2, SeekWalkoutShotWaitRead / SeekWalkoutShotWaitEnd).
        self.assertEqual(walk.count("task.walkoutGen"), 7)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("out.walkoutGen = state.startGen;", update)
        # The start branch hands over the new task's generation inside the start lock (review R2-M1).
        start = update[update.index("WalkoutStart(record.state, self, sonGuid, nowMs, cycleNo, dueInMs);"):]
        gen = start.index("out.walkoutGen = record.state.startGen;")
        self.assertLess(gen, start.index("\n    }\n"))  # before the lock's scope closes
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        self.assertIn("task.walkoutGen = state.walkoutGen;", find)
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        start = policy[policy.index("inline void WalkoutStart(SeekWalkoutState& s"):]
        start = start[:start.index("\n    }\n")]
        self.assertTrue(start.rstrip().endswith("++s.startGen;"))


if __name__ == "__main__":
    unittest.main()
