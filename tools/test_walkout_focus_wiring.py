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
        face = walk.index("if (faceFirst)\n                    return true;")
        stop = walk.index("if (faceFirst && !bot->IsStopped())\n                    ai->StopMoving();")
        turn = walk.index("sServerFacade.SetFacingTo(bot, task.source);")
        # Stop, then turn, then return: the stop's spline cannot undo the facing.
        self.assertLess(stop, turn)
        self.assertLess(turn, face)
        self.assertLess(face, walk.index("WalkoutCurrentCast(bot, task.source"))
        self.assertLess(face, walk.index("return ai->CastSpell(spell, task.source);") if "return ai->CastSpell(spell, task.source);" in walk
                        else walk.index("const bool cast = ai->CastSpell(spell, task.source);"))
        self.assertLess(face, walk.index("bot->InterruptNonMeleeSpells(false);"))
        self.assertIn("const bool faceFirst = WalkoutFaceFirst(focusOn, rulesOn, inFront);", walk)
        self.assertIn("const bool focusOn = sPlayerbotAIConfig.dungeonRunSeekWalkoutFocus;", walk)

    def test_a_pull_shot_in_flight_is_waited_for(self):
        walk = walkout_branch()
        wait = walk.index("if (focusOn && WalkoutPullShotWaits(task.walkoutPullCastMs, WorldTimer::getMSTime(),")
        self.assertLess(wait, walk.index("bot->InterruptNonMeleeSpells(false);"))
        self.assertLess(wait, walk.index("AbortSeekWalkoutUnspent(ai, bot, sonYd);"))
        self.assertIn("task.source->IsInCombat()))\n                    return true;", walk)
        self.assertIn("if (cast && focusOn)\n                    NoteSeekWalkoutPullCast(bot);", walk)
        note = body(ACTIONS, "void NoteSeekWalkoutPullCast(Player* bot)")
        self.assertIn("it->second.state.pullCastMs = WorldTimer::getMSTime();", note)
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", note)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("if (rules.shotStall && WalkoutNoShotAbortDue(WalkoutShotStalled(state, nowMs), state.pullCastMs, nowMs))",
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
        self.assertIn("it->second.state.puller != bot->GetObjectGuid().GetRawValue()", read)
        self.assertIn("shot = it->second.state.shot;", read)
        self.assertIn("in.recordedPuller = true;", read)
        self.assertIn("in.sinceStartMs = nowMs - startMs;", read)
        self.assertIn("if (WalkoutFocusHolds(in, action) && WalkoutPreemptRowDue(it->second.preemptLogStartMs, startMs))", read)
        self.assertIn("it->second.preemptLogStartMs = startMs;", read)

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


if __name__ == "__main__":
    unittest.main()
