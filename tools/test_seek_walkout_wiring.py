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
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son)")
        self.assertIn("PickWalkoutPuller(", pick)
        self.assertIn("WalkoutRoleOf(", pick)
        for guard in ("PlayerbotAI::IsTank(member)", "PlayerbotAI::IsHeal(member)", "boss->GetVictim() == member",
                      "TopTwoThreat(boss, top)", "GetCharmerGuid()", "kHakkarCauseInsanitySpell"):
            self.assertIn(guard, pick)
        self.assertIn("getThreatList()", body(ACTIONS, "void TopTwoThreat(Unit* boss, ObjectGuid (&top)[2])"))

    def test_the_seek_read_runs_the_task(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, walkoutWanted, out)", read)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        for call in ("WalkoutCheck(", "WalkoutMayStart(", "WalkoutStart(", "WalkoutNoteShot(", "WalkoutFinish(",
                     "FindSeekWalkoutPuller(ai, bot, boss, scan.son) != bot"):
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
        self.assertIn("WalkoutShotPoint(", walk)
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
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss, Unit* son)")
        self.assertIn("WalkoutShotSpell(cls)", pick)
        self.assertIn("memberAi->CanCastSpell(spell, son, 0, nullptr, true, true)", pick)
        self.assertIn("c.canShoot =", pick)
        walk = self.walk()
        # At the stand the exact cast is checked; a cast that fails aborts at once and spends nothing.
        self.assertIn("ai->CanCastSpell(spell, task.source)", walk)
        self.assertLess(walk.index("ai->CanCastSpell(spell, task.source)"), walk.index("ai->CastSpell(spell, task.source)"))
        self.assertIn("AbortSeekWalkoutUnspent(ai, bot);", walk)
        self.assertIn("WalkoutAbortUnspent(", body(ACTIONS, "void AbortSeekWalkoutUnspent(PlayerbotAI* ai, Player* bot)"))

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
        self.assertIn("WalkoutPullerUsable(", update)
        self.assertIn("InTopTwoThreat(boss, puller)", update)

    def test_stale_state_is_dropped(self):
        exempt = body(ACTIONS, "bool SeekWalkoutExemptsBot(Player* bot)")
        self.assertIn("SweepSeekWalkouts(", exempt)
        sweep = body(ACTIONS, "void SweepSeekWalkouts(Map* map, std::uint32_t nowMs)")
        self.assertIn("kSeekWalkoutStaleMs", sweep)
        self.assertIn("IsInCombat()", sweep)
        self.assertIn("IsAlive", sweep)


if __name__ == "__main__":
    unittest.main()
