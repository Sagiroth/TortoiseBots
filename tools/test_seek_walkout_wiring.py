"""Wiring pins for the seek walk-out pull (Hakkar's Sons stand ~78 yd out, past the anchored shot).

The rules are pure and tested in tools/test_seek_walkout_policy.cpp (the puller's role and exclusions, the task's
start limits, its ends and the exemption window, the shot and back points). These pins only prove the live code calls
them: one ranged puller leaves the anchor while its task runs, shoots the Son once and walks back.
Run: python3 -m unittest tools/test_seek_walkout_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def walkout_branch():
    fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
    return fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]


class SeekWalkoutWiring(unittest.TestCase):
    def test_the_puller_alone_is_exempt_from_the_anchor(self):
        # The exemption lives in the one live-anchor read, so every clamp and the ChaseTo refusal drop for the puller
        # while its task runs, and come back the moment it ends.
        live = body(ACTIONS, "const BossTacticRow* ai::LiveBossAnchorRow(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (SeekWalkoutExemptsBot(bot))\n        return nullptr;", live)
        self.assertLess(live.index("SeekWalkoutExemptsBot(bot)"), live.index("ScanBossTactics(ai, bot)"))
        exempt = body(ACTIONS, "bool SeekWalkoutExemptsBot(Player* bot)")
        self.assertIn("WalkoutExempts(", exempt)
        self.assertIn("LiveBossAnchor(ai, bot, anchor)", body(MOVEMENT, "bool MovementAction::ChaseTo(WorldObject* obj, float distance, float angle)"))

    def test_the_puller_is_picked_by_the_policy(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("RankWalkoutPullers(", pick)
        self.assertIn("WalkoutRoleOf(", pick)
        for guard in ("PlayerbotAI::IsTank(member)", "PlayerbotAI::IsHeal(member)", "boss->GetVictim() == member",
                      "WalkoutThreatTooHigh(", "GetCharmerGuid()", "kHakkarCauseInsanitySpell"):
            self.assertIn(guard, pick)

    def test_the_seek_read_runs_the_task(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, out.dueInMs, walkoutWanted, out)", read)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        for call in ("WalkoutCheck(", "WalkoutMayStartFor(", "WalkoutStart(", "WalkoutNoteShot(", "WalkoutFinish(",
                     "scan.puller != self"):
            self.assertIn(call, update)
        # Its one shot is the pull: exempt from the seek hold like the other pullers' until the shot lands, then held.
        self.assertIn("out.puller = true;", update)
        self.assertIn("out.pullTarget = scan.son;", update)
        self.assertIn("out.puller = !state.shot;", update)
        self.assertIn("out.pullTarget = !state.shot && sonOk ? son : nullptr;", update)
        self.assertIn('"seek pull: ranged walk-out %s -> %u"', ACTIONS)
        self.assertIn('"seek pull: walk-out %s"', ACTIONS)
        # A re-pull or an evade ends any walk-out with the seek record.
        self.assertGreaterEqual(read.count("ForgetSeekWalkout(ai, bot->GetInstanceId())"), 2)

    def test_the_task_shoots_once_and_walks_back(self):
        self.assertIn("bool walkout = false;", ACTIONS_H)
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("state.walkout", tank)
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        self.assertIn("WalkoutShotRangeYd(bot->GetClass() == CLASS_HUNTER)", walk)
        self.assertIn("WalkoutBackPoint(", walk)
        self.assertIn("IsWithinLOSInMap(task.source)", walk)
        # The clamp is asked even here: a no-op while the task exempts the bot, the anchor again once it lapses.
        self.assertLess(walk.index("ClampMoveToBossAnchor(ai, bot, x, y, z)"), walk.index("MoveTo(bot->GetMapId(), x, y, z"))


class SeekWalkoutRound1(unittest.TestCase):
    def walk(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        return fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]

    def test_the_pet_is_held_and_the_shot_is_cast_directly(self):
        walk = self.walk()
        # No generic Attack: it sends the pet in, the pet takes the Son and keeps it outside.
        self.assertNotIn("Attack(bot, task.source)", walk)
        self.assertNotIn('DoSpecificAction("shoot"', walk)
        self.assertIn("HoldWalkoutPet(ai, bot);", walk)
        self.assertLess(walk.index("HoldWalkoutPet(ai, bot);"), walk.index("ai->CastSpell(spell, task.source)"))
        hold = body(ACTIONS, "void HoldWalkoutPet(PlayerbotAI* ai, Player* bot)")
        for call in ("SetReactState(REACT_PASSIVE)", "WalkoutSavePetStance(g_petHolds,", "GetCommandState()",
                     "pet->GetObjectGuid().GetRawValue()",
                     "AttackStop()", "MoveFollow(bot, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE)"):
            self.assertIn(call, hold)

    def test_the_pet_is_released_on_its_bots_next_update_after_any_end(self):
        # Every end (Back, the walk back done, a re-pull, the boss's death or combat exit, a stale sweep) leaves the
        # stance in g_petHolds; the bot's own next update hands back the react state and the command.
        release = body(ACTIONS, "void ai::ReleaseWalkoutPet(Player* bot)")
        for call in ("WalkoutPetRelease(", "WalkoutTakePetStance(g_petHolds,", "SetReactState(static_cast<ReactStates>(stance.react))",
                     "SetCommandState(static_cast<CommandStates>(stance.command))"):
            self.assertIn(call, release)
        self.assertIn("void ReleaseWalkoutPet(Player* bot);", ACTIONS_H)
        # Only the pet the stance was taken from gets it back; stale entries (a puller that logged out or was dismissed
        # mid-hold) age out on any bot's release, once a second at most.
        self.assertIn("WalkoutPetStanceApplies(stance, pet ? pet->GetObjectGuid().GetRawValue() : 0)", release)
        self.assertIn("WalkoutSweepPetHolds(g_petHolds, nowMs)", release)
        self.assertLess(release.index("WalkoutSweepPetHolds("), release.index("g_petHolds.find(self)"))
        ai_cpp = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
        update = ai_cpp[ai_cpp.index("void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)"):]
        self.assertLess(update.index("ai::ReleaseWalkoutPet(bot);"), update.index("++aiUpdateTick;"))
        # No record path restores on its own (another map thread may own the puller).
        self.assertNotIn("RestoreWalkoutPet(", ACTIONS)

    def test_only_a_bot_that_can_fire_is_picked(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("WalkoutShotSpell(cls)", pick)
        self.assertIn("memberAi->CanCastSpell(spell, son, 0, nullptr, true, true)", pick)
        self.assertIn("c.canShoot =", pick)
        walk = self.walk()
        # At the stand the exact cast is checked; a cast that fails aborts at once and spends nothing.
        self.assertIn("ai->CanCastSpell(spell, task.source)", walk)
        self.assertLess(walk.index("ai->CanCastSpell(spell, task.source)"), walk.index("ai->CastSpell(spell, task.source)"))
        self.assertIn("AbortSeekWalkoutUnspent(ai, bot, sonYd);", walk)
        self.assertIn("WalkoutCastFailed(", body(ACTIONS, "void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot, float sonYd)"))

    def test_any_end_walks_back_without_attacking(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutFinish(it->second.state, end, nowMs)", update)
        self.assertIn("WalkoutReturning(", update)
        self.assertIn("WalkoutReturnDone(", update)
        walk = self.walk()
        back = walk[walk.index("WalkoutBackPoint(") - 400:]
        self.assertIn("InterruptSpell(CURRENT_AUTOREPEAT_SPELL)", back)
        self.assertIn("bot->AttackStop();", back)

    def test_top_threat_aborts(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutPullerProblem(", update)
        # Threat no longer aborts a running task (only the victim rule does); it only gates the pick.
        self.assertNotIn("SeekWalkoutHighThreat", ACTIONS)
        self.assertNotIn("WalkoutThreatTooHigh(", update)
        self.assertNotIn("TopTwoThreat", ACTIONS)

    def test_stale_state_is_dropped(self):
        exempt = body(ACTIONS, "bool SeekWalkoutExemptsBot(Player* bot)")
        self.assertIn("SweepSeekWalkouts(", exempt)
        sweep = body(ACTIONS, "void SweepSeekWalkouts(Map* map, std::uint32_t nowMs)")
        self.assertIn("kSeekWalkoutStaleMs", sweep)
        self.assertIn("IsInCombat()", sweep)
        self.assertIn("IsAlive", sweep)


class SeekWalkoutLiveFixes(unittest.TestCase):
    def test_the_stand_stays_on_the_platform(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("FindSeekWalkoutStand(bot, shooter, sons[static_cast<std::size_t>(i)],", update)
        stand = body(ACTIONS, "bool FindSeekWalkoutStand(Player* bot, const BossAnchorCircle& c, Unit* son, float rangeYd,")
        for call in ("WalkoutStandCandidates(c, son->GetPositionX(), son->GetPositionY())", "GetHeight(", "WalkoutStandOk(",
                     "isInLineOfSight("):
            self.assertIn(call, stand)
        self.assertIn('"seek pull: walk-out no stand"', ACTIONS)
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        # The stand comes from the task (the platform search), never from the Son's height.
        self.assertNotIn("z = task.source->GetPositionZ();", walk)
        self.assertIn("x = task.x;", walk)
        self.assertIn("z = task.z;", walk)

    def test_the_puller_is_inside_with_a_path(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("c.inside = InsideAnchor(", pick)
        self.assertIn("RankWalkoutPullers(candidates)", pick)
        self.assertIn("SeekWalkoutPathOk(member, standX, standY, standZ)", pick)
        path = body(ACTIONS, "bool SeekWalkoutPathOk(Player* member, float x, float y, float z)")
        self.assertIn("PathFinder path(member);", path)
        self.assertIn("PATHFIND_NOPATH", path)

    def test_the_seek_walk_does_not_hold_the_puller(self):
        # "boss tactics seek" (ACTION_EMERGENCY + 5) outranks "boss tactics seek tank" (ACTION_HIGH + 6), and its hold at
        # the gather stopped the puller every tick: the walk-out bot is no seeker while its task runs.
        seek = body(ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (state.walkout)\n        return task;", seek)
        self.assertLess(seek.index("if (state.walkout)"), seek.index("SeekWanted("))

    def test_moves_are_logged_and_two_failures_abort(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        self.assertIn("NoteSeekWalkoutMove(ai, bot, moved", walk)
        note = body(ACTIONS, "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved,")
        self.assertIn("WalkoutNoteMove(", note)
        self.assertIn('"move failed"', note)
        self.assertIn('"seek pull: walk-out abort %s dist=%.1f stand=%.1f,%.1f,%.1f moved=%d path=%u len=%.1f why=%s"', ACTIONS)

    def test_the_hunter_pet_comment_is_gone(self):
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertNotIn("a pet to tank", policy)


class SeekWalkoutRound3(unittest.TestCase):
    def test_a_move_failed_abort_is_unspent_and_excludes_the_puller(self):
        note = body(ACTIONS, "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved,")
        self.assertIn("WalkoutAbortUnspent(it->second.state, nowMs)", note)
        self.assertIn("WalkoutExclude(it->second.state, bot->GetObjectGuid().GetRawValue(), nowMs)", note)
        self.assertNotIn("WalkoutFinish(", note)
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("c.excluded = WalkoutExcluded(state, c.guid, nowMs);", pick)

    def test_the_scan_takes_the_nearest_son_with_a_stand(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("PickWalkoutSon(sonYd,", update)
        self.assertIn("FindSeekWalkoutStand(bot, shooter, sons[static_cast<std::size_t>(i)],", update)


class SeekWalkoutRound4(unittest.TestCase):
    def test_an_excluded_puller_is_not_restarted_from_a_fresh_scan(self):
        note = body(ACTIONS, "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved,")
        self.assertIn("it->second.scanned = false;", note)
        self.assertLess(note.index("WalkoutExclude("), note.index("it->second.scanned = false;"))
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (!WalkoutMayStartFor(record.state, cycleNo, sonGuid, self, nowMs, dueInMs))", update)
        self.assertLess(update.index("WalkoutMayStartFor("), update.index("WalkoutStart(record.state"))


class SeekWalkoutT1105h(unittest.TestCase):
    def test_the_stand_is_replanned_from_the_sons_live_position(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        replan = update[update.index("WalkoutReplanDue("):]
        self.assertIn("FindSeekWalkoutStand(bot, shooter, son, WalkoutShotRangeYd(bot->GetClass() == CLASS_HUNTER)", replan)
        self.assertIn("SeekWalkoutPathOk(bot, standX, standY, standZ)", replan)
        self.assertIn("WalkoutNoteStand(it->second.state, found, standX, standY, standZ, nowMs)", replan)
        self.assertIn('LogSeekWalkoutAbort(ai, "no stand"', replan)

    def test_the_cast_stops_before_an_outbound_move(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        stop = walk.index("bot->InterruptNonMeleeSpells(true);")
        self.assertLess(stop, walk.index("MoveTo(bot->GetMapId(), x, y, z"))
        self.assertIn("lastMove.moveFailReason", walk)
        self.assertIn("lastMove.movePathType", walk)


class SeekWalkoutT1105j(unittest.TestCase):
    def test_a_failed_walkout_may_retry_once_in_the_cycle(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (!wanted || !WalkoutCycleOpen(state, cycleNo) ||", update)
        self.assertIn("WalkoutStart(record.state, self, sonGuid, nowMs, cycleNo, dueInMs);", update)
        self.assertIn("reopened = WalkoutFinish(it->second.state, end, nowMs);", update)
        # A walked Son that died is forgotten before the scan, so its respawn is the retry's Son.
        self.assertIn("WalkoutForgetGoneSons(it->second.state,", update)
        self.assertLess(update.index("WalkoutForgetGoneSons("), update.index("state = it->second.state;"))

    def test_a_timeout_logs_the_shot_and_the_distance(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("LogSeekWalkoutTimeout(ai, state.shot,", update)
        self.assertIn("spellId, los, z, standYd);", update)
        for field in ("shot=%d", "dist=%.1f", "retry=%d", "spell=%u", "los=%d", "z=%.1f", "stand=%.1f"):
            self.assertIn(field, ACTIONS)

    def test_the_retry_time_is_read_at_the_start(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("!WalkoutRetryStartInTime(state, cycleNo, nowMs)", update)

    def test_a_cast_fail_excludes_the_puller_and_rescans(self):
        abort = body(ACTIONS, "void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot, float sonYd)")
        self.assertIn("WalkoutCastFailed(it->second.state, bot->GetObjectGuid().GetRawValue(), WorldTimer::getMSTime());",
                      abort)
        self.assertIn("it->second.scanned = false;", abort)


class SeekWalkoutT1106b(unittest.TestCase):
    def test_a_cast_on_the_boss_is_interrupted_and_the_son_shot(self):
        walk = walkout_branch()
        # Only a cast aimed at the Son is waited for: a hunter's Auto Shot still on Hakkar (or a wand's Shoot) is
        # not the pull, so it is interrupted and the shot goes out now.
        self.assertIn("Spell* const cast = WalkoutCurrentCast(bot, task.source, castOnSon);", walk)
        wait = walk.index("if (WalkoutShotWaits(castOnSon, cast != nullptr))")
        interrupt = walk.index("bot->InterruptNonMeleeSpells(false);")
        self.assertLess(wait, interrupt)
        # The interrupt comes before the cast check: a generic cast on the boss would refuse the new one otherwise.
        self.assertLess(interrupt, walk.index("ai->CanCastSpell(spell, task.source)"))
        self.assertLess(interrupt, walk.index("ai->CastSpell(spell, task.source)"))
        self.assertNotIn("IsNonMeleeSpellCasted(false, false, false)", walk)
        # One walk-out per cycle: the retry paths stay, but a retry is never allowed.
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertIn("constexpr int kSeekWalkoutMaxPerCycle = 1;", policy)
        for fn in ("inline bool WalkoutRetryAllowed(", "inline bool WalkoutRetryStartInTime("):
            self.assertIn(fn, policy)

    def test_the_current_cast_spans_the_three_slots(self):
        current = body(ACTIONS, "Spell* WalkoutCurrentCast(Player* bot, Unit* son, bool& onSon)")
        self.assertIn("CURRENT_GENERIC_SPELL", current)
        self.assertIn("CURRENT_CHANNELED_SPELL", current)
        self.assertIn("CURRENT_AUTOREPEAT_SPELL", current)
        self.assertIn("m_targets.getUnitTarget() == son", current)

    def test_the_timeout_logs_the_cast_the_los_and_the_stand(self):
        log = body(ACTIONS, "void LogSeekWalkoutTimeout(PlayerbotAI* ai, bool shot, float sonYd, bool retry,")
        for field in ("spell=%u", "los=%d", "z=%.1f", "stand=%.1f"):
            self.assertIn(field, log)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        timeout = update[update.index("if (end == WalkoutEnd::Timeout)"):]
        self.assertIn("WalkoutCurrentCast(puller, son, castOnSon)", timeout)
        self.assertIn("IsWithinLOSInMap(son)", timeout)
        self.assertIn("GetPositionZ()", timeout)
        self.assertIn("GetDistance2d(state.standX, state.standY)", timeout)


if __name__ == "__main__":
    unittest.main()
