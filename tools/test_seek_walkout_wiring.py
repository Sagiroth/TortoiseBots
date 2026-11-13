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
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, out.dueInMs, walkoutWanted, fightSons, out)", read)
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
        self.assertGreaterEqual(read.count("ForgetSeekWalkout(ai, bot->GetInstanceId(),"), 2)

    def test_the_siphon_clock_cuts_a_no_sight_task(self):
        # DungeonRunSeekSiphonClock: the rules are lowered after the row's own, and an unshot task that never stood in
        # shot position aborts "no sight" inside the active branch, before the no-shot stall.
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunSeekSiphonClock)", update)
        self.assertLess(update.index("WalkoutRulesFromRow("), update.index("WalkoutSiphonClockRules("))
        active = update[update.index("if (WalkoutActive(state))"):]
        self.assertIn("WalkoutNoSightCutDue(state, nowMs, rules, sPlayerbotAIConfig.dungeonRunSeekWalkoutFaceCast)",
                      active)
        self.assertLess(active.index("WalkoutNoSightCutDue("), active.index("bool const stallDue"))
        self.assertIn('LogSeekWalkoutAbort(ai, "no sight"', active)
        self.assertIn("WalkoutSiphonClockRules(rules)", update)
        self.assertIn("floor=%u", ACTIONS)
        # Round-1 L2/L3: the cut also needs the focus (it writes the pull cast) and a puller and Son that can still
        # act, so a dead puller or Son ends as gone and is never benched as no sight.
        self.assertIn("if (pullerOk && sonOk && sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus &&\n", active)
        self.assertLess(active.index("if (pullerOk && sonOk"), active.index("LogSeekWalkoutAbort(ai, \"no sight\""))
        # Round-1 L1: the shared state decides the cut again under its own lock, before the abort changes it.
        cut_block = active[:active.index("bool const stallDue")]
        self.assertIn("WalkoutNoSightCutDue(it->second.state, nowMs, rules,", cut_block)
        self.assertLess(cut_block.index("WalkoutSnapshotCurrent(it->second.state, state)"),
                        cut_block.index("WalkoutAbortUnspent(it->second.state, nowMs, rules);"))
        self.assertGreater(cut_block.index("if (cut)"), cut_block.index("WalkoutAbortUnspent(it->second.state, nowMs, rules);"))

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
        self.assertIn("AbortSeekWalkoutUnspent(ai, bot, sonYd, task.walkoutGen);", walk)
        self.assertIn("WalkoutCastFailed(", body(ACTIONS, "void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot, float sonYd, std::uint32_t gen)"))

    def test_any_end_walks_back_without_attacking(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutFinish(it->second.state, end, nowMs, rules)", update)
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
        self.assertIn('"seek pull: walk-out no stand sons=%d reach=%.0f hunter_pass=%d"', ACTIONS)
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        # The stand comes from the task (the platform search), never from the Son's height.
        self.assertNotIn("z = task.source->GetPositionZ();", walk)
        self.assertIn("x = task.x;", walk)
        self.assertIn("z = task.z;", walk)

    def test_the_puller_is_inside_with_a_path(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("rules.platformPick\n            ? WalkoutOnPlatform(", pick)
        self.assertIn(": InsideAnchor(circle, member->GetPositionX(), member->GetPositionY());", pick)
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
        self.assertIn("WalkoutNoteOutboundMove(", note)  # WalkoutNoteMove behind the stand-sight rule
        self.assertIn('"move failed"', note)
        self.assertIn('"seek pull: walk-out abort %s dist=%.1f stand=%.1f,%.1f,%.1f moved=%d path=%u len=%.1f why=%s%s"', ACTIONS)

    def test_the_hunter_pet_comment_is_gone(self):
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertNotIn("a pet to tank", policy)


class SeekWalkoutRound3(unittest.TestCase):
    def test_a_move_failed_abort_is_unspent_and_excludes_the_puller(self):
        note = body(ACTIONS, "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved,")
        self.assertIn("WalkoutAbortUnspent(it->second.state, nowMs, it->second.rules)", note)
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
        self.assertIn("if (!WalkoutMayStartFor(record.state, cycleNo, sonGuid, self, nowMs, dueInMs, rules))", update)
        self.assertLess(update.index("WalkoutMayStartFor("), update.index("WalkoutStart(record.state"))


class SeekWalkoutT1105h(unittest.TestCase):
    def test_the_stand_is_replanned_from_the_sons_live_position(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        replan = update[update.index("WalkoutReplanDue("):]
        self.assertIn("FindSeekWalkoutStand(bot, shooter, son, WalkoutShotRangeYd(bot->GetClass() == CLASS_HUNTER)", replan)
        self.assertIn("SeekWalkoutPathOk(bot, standX, standY, standZ)", replan)
        self.assertIn("WalkoutNoteStand(it->second.state, found, standX, standY, standZ, nowMs, inShot)", replan)
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
        self.assertIn("if (!wanted || !WalkoutCycleOpen(state, cycleNo, rules) ||", update)
        self.assertIn("WalkoutStart(record.state, self, sonGuid, nowMs, cycleNo, dueInMs);", update)
        self.assertIn("reopened = WalkoutFinish(it->second.state, end, nowMs, rules);", update)
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
        self.assertIn("!WalkoutRetryStartInTime(state, cycleNo, nowMs, rules)", update)

    def test_a_cast_fail_excludes_the_puller_and_rescans(self):
        abort = body(ACTIONS, "void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot, float sonYd, std::uint32_t gen)")
        self.assertIn("WalkoutCastFailed(it->second.state, bot->GetObjectGuid().GetRawValue(), WorldTimer::getMSTime(),",
                      abort)
        self.assertIn("it->second.rules);", abort)
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
        # The cast's target is compared by guid: its cached pointer can point at a unit the map freed this tick.
        self.assertIn("m_targets.getUnitTargetGuid() == sonGuid", current)
        self.assertNotIn("m_targets.getUnitTarget()", current)

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


class SeekWalkoutSeekPull(unittest.TestCase):
    """The seek_pull row (flag AiPlayerbot.DungeonRunSeekPullCycle): walk 51's per-cycle rules."""

    def test_the_flag_arms_the_rules(self):
        cfg = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekPullCycle", false)', cfg)
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertIn("inline WalkoutCycleRules WalkoutRulesFromRow(int priority, float windowSec, float seekLeadSec = 0.0f)", policy)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("SeekPullRow(", update)
        self.assertIn("WalkoutRulesFromRow(", update)

    def test_the_walked_list_is_per_cycle(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        # Right after the gone Sons are forgotten, the earlier cycles' Sons are dropped.
        self.assertIn("if (rules.perCycleWalked)\n                WalkoutForgetEarlierCycles(it->second.state, cycleNo);", update)
        self.assertLess(update.index("WalkoutForgetGoneSons(it->second.state,"), update.index("WalkoutForgetEarlierCycles("))

    def test_a_stalled_shot_aborts_unspent(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("rules.shotStall && WalkoutNoShotAbortDue(stallDue, state.pullCastMs, nowMs)", update)
        stall = update[update.index("rules.shotStall && WalkoutNoShotAbortDue(stallDue"):]
        self.assertIn("WalkoutAbortUnspent(it->second.state, nowMs, rules)", stall)
        self.assertIn("WalkoutExclude(it->second.state, state.puller, nowMs)", stall)
        self.assertIn('LogSeekWalkoutAbort(ai, "no shot"', stall)
        self.assertIn('"seek pull: walk-out abort %s dist=%.1f stand=%.1f,%.1f,%.1f moved=%d path=%u len=%.1f why=%s%s"',
                      ACTIONS)

    def test_the_action_notes_the_arrival_at_the_stand(self):
        walk = walkout_branch()
        self.assertIn("NoteSeekWalkoutAtStand(bot, task.walkoutGen);", walk)
        self.assertIn("kSeekWalkoutStandArriveYd", walk)
        self.assertIn("NoteSeekWalkoutAtStand", ACTIONS)
        note = body(ACTIONS, "void NoteSeekWalkoutAtStand(Player* bot, std::uint32_t gen)")
        self.assertIn("WalkoutNoteAtStand(it->second.state, nowMs)", note)

    def test_the_pick_and_the_abort_share_the_top_tank_reference(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("LiveTopTankThreat(", pick)
        self.assertIn("WalkoutPickThreatTooHigh(", pick)
        # The default rules keep the old main-tank reference; the seek_pull branch never reads it.
        self.assertIn("rules.pickTopTankThreat\n                ? WalkoutPickThreatTooHigh(", pick)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("LiveTopTankThreat(", update)
        helper = body(ACTIONS, "float LiveTopTankThreat(Group* group, Unit* boss, Player* except)")
        self.assertIn("WalkoutTopTankThreat(", helper)

    def test_the_hunter_reach_and_the_fan_widen_the_scan(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("rules.hunterReachScan", update)
        self.assertIn("WalkoutRulesFromRow(", update)
        stand = body(ACTIONS, "bool FindSeekWalkoutStand(Player* bot, const BossAnchorCircle& c, Unit* son, float rangeYd,")
        self.assertIn("WalkoutStandCandidatesFan(", stand)
        self.assertIn("rules.standFan", stand)

    def test_the_no_son_row_is_throttled(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn('"seek pull: walk-out no son walked=%d"', update)
        self.assertIn("nowMs - record.noSonLogMs >= 10000", update)
        self.assertIn("nowMs - record.noStandLogMs >= 10000", update)

    def test_the_start_row_carries_the_scan(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        for field in ("-> %u due=%u try=%d reach=%.0f ray=%.0f",):
            self.assertIn(field, update)

    def test_the_kill_lead_is_the_rows(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekLateEngagedBurnWithin(", read)
        self.assertIn("SeekPullRow(", read)
        self.assertIn("kSeekLateEngageMs", read)
        # The row's own param is the lead, not a fixed 30 s the row is only looked up for.
        self.assertIn("lateLeadMs = static_cast<std::uint32_t>(pull->param * 1000.0f);", read)


class SeekWalkoutRound2(unittest.TestCase):
    """The seek_pull gate text, the fan literal and the glue calls a cold review found unpinned."""

    def test_the_seek_pull_row_is_read_only_behind_the_flag(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        gated = ("if (sPlayerbotAIConfig.dungeonRunSeekPullCycle)\n"
                 "    {\n"
                 "        if (const BossTacticRow* const pull =\n"
                 "                GetBossTactics().SeekPullRow(")
        for fn in (update, read):
            self.assertIn(gated, fn)
            # The row lookup and its use sit inside the gate's block, closed right after it.
            block = fn[fn.index(gated) + len(gated):]
            self.assertIn(");", block)
            self.assertLess(block.index(");"), block.index("    }\n"))

    def test_the_stand_fan_keeps_the_son_heading_first(self):
        stand = body(ACTIONS, "bool FindSeekWalkoutStand(Player* bot, const BossAnchorCircle& c, Unit* son, float rangeYd,")
        self.assertIn("for (float const offset : {0.0f, 10.0f, -10.0f, 20.0f, -20.0f})", stand)

    def test_the_glue_paths_keep_their_arguments(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        # The per-cycle walked list arms the free, and a landed shot never frees its Son.
        self.assertIn("bool const freeSon = rules.perCycleWalked && end == WalkoutEnd::Abort && !it->second.state.shot &&",
                      update)
        # The hunter-only pass reaches the pick, and a no-stand abort keeps the Son walked under the seek row.
        self.assertIn("state, nowMs, hunterOnly, rules, &scan.tally);", update)
        self.assertIn("WalkoutAbortUnspent(it->second.state, nowMs, rules, !rules.perCycleWalked);", update)

    def test_the_no_puller_row_is_throttled(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        # The row's info1 keeps its old prefix; the scan's tally rides in info2 with the threat reference.
        self.assertIn('"seek pull: walk-out no puller stand=%.1f,%.1f reach=%.0f"', update)
        self.assertIn("char info1[96];", update)
        self.assertIn("WalkoutRejectText(scan.tally).c_str()", update)
        self.assertIn("if (rules.perCycleWalked && !scan.blocked && scan.stand && scan.puller == 0 &&", update)
        self.assertIn("nowMs - record.noPullerLogMs >= 10000", update)
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertIn('" top=%.0f low=%.0f"', policy)

    def test_a_late_start_returns_before_the_scan(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        gate = ("if (wanted && sPlayerbotAIConfig.dungeonRunSeekPullCycle && rules.firstStartMinMs != 0 &&\n"
                "        dueInMs < rules.firstStartMinMs)")
        self.assertIn(gate, update)
        late = update[update.index(gate):]
        self.assertIn('"seek pull: walk-out late due=%u need=%u"', late)
        self.assertIn("nowMs - record.lateLogMs >= 10000", late)
        # Only a cycle that could still start logs the late row.
        self.assertIn("if (!WalkoutCycleOpen(state, cycleNo, rules))\n            return;", late)
        # The late row and return come before the start scan's grid walk.
        self.assertLess(update.index(gate), update.index("The start check's scan"))


class SeekWalkoutWalk52(unittest.TestCase):
    """Walk 52's fixes: the platform pick, a mid-cast puller, the first-charm gate, the seek lead, facing and rows."""

    def test_the_pick_stands_on_the_platform_and_waits_for_a_cast(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertIn("c.inside = rules.platformPick\n            ? WalkoutOnPlatform(BossAnchorDistance2d(circle, member->GetPositionX(), member->GetPositionY()),\n                                member->GetPositionZ())\n            : InsideAnchor(circle, member->GetPositionX(), member->GetPositionY());", pick)
        self.assertIn("if (rules.pickCastLater)", pick)
        self.assertIn("CanCastSpell(spell, son, 0, nullptr, true, true, false, &r)", pick)
        self.assertIn("WalkoutShotReadyLater(ok, r == SPELL_FAILED_SPELL_IN_PROGRESS,", pick)
        self.assertIn("SPELL_FAILED_SPELL_IN_PROGRESS", pick)
        self.assertIn("SPELL_FAILED_NOT_READY", pick)
        # Without the rule the old plain check stays.
        self.assertIn("c.canShoot = memberAi->CanCastSpell(spell, son, 0, nullptr, true, true);", pick)
        self.assertIn("tally->top = topTankThreat;", pick)
        self.assertIn("tally->low = memberThreat;", pick)

    def test_the_first_charm_gate(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (wanted && !WalkoutFightOldEnough(rules, out.fightMs))", update)
        self.assertLess(update.index("!WalkoutFightOldEnough("), update.index("The start check's scan"))
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("fightMs = nowMs - record.engageMs;", read)
        self.assertIn("out.fightMs = fightMs;", read)

    def test_the_start_lead_is_the_seek_windows(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutRulesFromRow(pull->priority, pull->windowSec, row.windowSec)", update)

    def test_the_puller_faces_the_son_behind_the_seek_row(self):
        walk = walkout_branch()
        self.assertIn("const bool rulesOn = SeekPullRulesOn(bot->GetMapId(), task.row);", walk)
        self.assertIn("const bool inFront = sServerFacade.isInFront(bot, task.source, sPlayerbotAIConfig.sightDistance,\n"
                      "                                                             CAST_ANGLE_IN_FRONT);", walk)
        # The turn's rule is the pure WalkoutFaceThenCast (flag off: face while the rules are on and not in front).
        self.assertIn("if (step.face)\n"
                      "                    sServerFacade.SetFacingTo(bot, task.source, step.force);", walk)
        self.assertLess(walk.index("sServerFacade.SetFacingTo(bot, task.source, step.force)"),
                        walk.index("WalkoutCurrentCast(bot, task.source"))
        helper = body(ACTIONS, "bool SeekPullRulesOn(std::uint32_t mapId, const BossTacticRow* seekRow)")
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekPullCycle", helper)
        self.assertIn("GetBossTactics().SeekPullRow(mapId, seekRow->bossEntry, seekRow->targetEntry)", helper)
        # The arrival note is gated by the same helper.
        self.assertIn("if (SeekPullRulesOn(bot->GetMapId(), task.row) &&\n"
                      "                (bot->GetDistance2d(task.x, task.y) <= kSeekWalkoutStandArriveYd ||", walk)

    def test_the_stall_clock_keeps_running_in_shot_position(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("bool const inShot = rules.shotStall && son &&", update)
        self.assertIn("WalkoutNoteStand(it->second.state, found, standX, standY, standZ, nowMs, inShot)", update)
        policy = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()
        self.assertIn("bool keepClock = false", policy)
        self.assertIn("if (moved && !keepClock)", policy)

    def test_the_no_shot_row_says_what_the_puller_was_doing(self):
        self.assertIn(" los=%d inrange=%d spell=%u onson=%d front=%d moving=%d dz=%.1f", ACTIONS)
        self.assertIn('LogSeekWalkoutAbort(ai, "no shot", puller && son ? puller->GetDistance(son) : -1.0f, state, info2);',
                      ACTIONS)
        self.assertIn("char info1[320];", ACTIONS)

    def test_the_start_row_carries_the_threat_ratio(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("tr=%.2f", update)
        self.assertIn("LiveTopTankThreat(bot->GetGroup(), boss, nullptr)", update)
        self.assertIn("float const tr = topThreat > 0.0f ? boss->GetThreatManager().getThreat(bot) / topThreat : -1.0f;",
                      update)


class SeekWalkoutRefusalReasons(unittest.TestCase):
    """Every walk-out refusal row names why (diag-2): the pick's tally, the scan's counts, the reset's cause."""

    def update(self):
        return body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")

    def test_the_no_puller_row_carries_the_picks_rejections(self):
        update = self.update()
        # The tally lives in the scan struct, next to the rest of the scan's result.
        self.assertIn("WalkoutRejectTally tally;", update)
        self.assertIn("state, nowMs, hunterOnly, rules, &scan.tally);", update)
        # info1 is byte-identical to before the tally (analysis scripts match on its prefix).
        self.assertIn('"seek pull: walk-out no puller stand=%.1f,%.1f reach=%.0f"', update)
        self.assertIn("LogSeekWalkout(ai, info1, WalkoutRejectText(scan.tally).c_str());", update)

    def test_the_scan_rows_names_their_numbers(self):
        update = self.update()
        self.assertIn('"seek pull: walk-out no stand sons=%d reach=%.0f hunter_pass=%d"', update)
        self.assertIn("static_cast<int>(sons.size())", update)
        self.assertIn('"seek pull: walk-out late due=%u need=%u"', update)
        self.assertIn("rules.firstStartMinMs / 1000", update)

    def test_the_reset_row_names_its_cause(self):
        forget = body(ACTIONS, "void ForgetSeekWalkout(PlayerbotAI* ai, std::uint32_t instanceId, const char* cause)")
        self.assertIn('" cause=%s"', forget)
        self.assertIn('LogSeekWalkoutAbort(ai, "reset", -1.0f, state, suffix);', forget)
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn('ForgetSeekWalkout(ai, bot->GetInstanceId(), "repull");', read)
        self.assertIn('ForgetSeekWalkout(ai, bot->GetInstanceId(), "evade");', read)
        self.assertLess(read.index('"repull");'), read.index('"evade");'))

    def test_the_pick_adds_no_game_state_read(self):
        # The tally only reads fields the pick already computed: its two cast checks (the plain one and the cast-later
        # one), the member/main-tank threats and the ranked path query, exactly as the walk-52 pick did.
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son,")
        self.assertEqual(pick.count("CanCastSpell("), 2)
        self.assertEqual(pick.count("getThreat("), 2)
        self.assertEqual(pick.count("SeekWalkoutPathOk("), 1)
        self.assertIn("WalkoutRejectTally* tally = nullptr", pick)
        # The members the loop never made a candidate (off-map, no AI, the hunter pass) and the post-loop pass.
        self.assertIn("++tally->members;", pick)
        self.assertIn("tally->Add(WalkoutReject::Away);", pick)
        self.assertIn("tally->Add(WalkoutReject::NoAi);", pick)
        self.assertIn("tally->Add(WalkoutReject::HunterOnly);", pick)
        self.assertIn("if (!WalkoutEligible(c))\n                tally->Add(WalkoutRejectOf(c));", pick)
        self.assertIn("tally->Add(WalkoutReject::NoPath);", pick)



class WalkoutStandSightWiring(unittest.TestCase):
    """DungeonRunSeekWalkoutStandSight (walk 56 HAKKAR-WHY F1)."""

    def test_the_flag_defaults_off(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        config_h = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        settings = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutStandSight", false)', config)
        self.assertIn("bool dungeonRunSeekWalkoutStandSight = false;", config_h)
        self.assertIn("AiPlayerbot.DungeonRunSeekWalkoutStandSight = 0", template)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekWalkoutStandSight`", settings)

    def test_the_stand_test_aims_the_core_way(self):
        fn = body(ACTIONS, "bool FindSeekWalkoutStand(Player* bot, const BossAnchorCircle& c, Unit* son, float rangeYd,")
        self.assertIn("bool const standSight = sPlayerbotAIConfig.dungeonRunSeekWalkoutStandSight;", fn)
        self.assertIn("WalkoutStandEyeZ(standSight, floor, bot->GetCollisionHeight())", fn)
        self.assertIn("WalkoutStandSightZ(standSight, son->GetPositionZ(), son->GetCollisionHeight()), true);", fn)
        self.assertNotIn("son->GetPositionZ() + 2.0f", fn)

    def test_note_move_calls_the_refusal_rule(self):
        fn = body(ACTIONS, "void NoteSeekWalkoutMove(PlayerbotAI* ai, Player* bot, bool moved, std::uint32_t failReason,")
        self.assertIn("WalkoutMoveRefusalCounts(sPlayerbotAIConfig.dungeonRunSeekWalkoutStandSight, moved, standYd,\n"
                      "                                     it->second.state.freeReplans);", fn)
        self.assertIn("abort = WalkoutNoteOutboundMove(it->second.state, moved, counts, WorldTimer::getMSTime());", fn)
        self.assertNotIn("WalkoutNoteMove(it->second.state, moved)", fn)
        walk = walkout_branch()
        self.assertIn("lastMove.movePathType ? lastMove.movePathType : probeType, probeYd,\n"
                      "                                bot->GetDistance2d(x, y), task.walkoutGen);", walk)


if __name__ == "__main__":
    unittest.main()
