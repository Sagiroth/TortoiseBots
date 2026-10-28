"""Wiring pins for the tank-first pull hold (turtle-agent#265).

The pure rule lives in ai/playerbot/PullJoinWindowPolicy.h (PullLeadReleases,
unit-tested in tools/test_pull_join_window_policy.cpp). These pins prove the
live rows call it: a rule nobody calls changes nothing in the game.

 - The hold release waits for the tank's lead on a run leader's pull
   (PullHoldExpiredTrigger asks the lead rule while the window runs, and the
   timer expiry is the ceiling that releases at once).
 - The run leader's party hold is lead-keyed; the chat pullback hold is not.
 - The landing stamps the window landed only where the spec says: an ordinary
   pull marks it at the shot, a pullback only when the tank is back.

Run: python3 -m unittest tools/test_pull_lead_release_wiring.py
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PULL_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/PullTriggers.cpp").read_text()
COMMANDS = (ROOT / "commands/BotCommandContext.cpp").read_text()
PULL_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/PullActions.cpp").read_text()
STATE_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BotStateActions.cpp").read_text()
PLAYERBOT_AI_CPP = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
PLAYERBOT_AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PullLeadReleaseWiring(unittest.TestCase):
    def test_hold_expiry_checks_the_tank_lead(self):
        fn = body(PULL_TRIGGERS, "bool PullHoldExpiredTrigger::IsActive()")
        self.assertIn("PullWindowLeadKeyed()", fn)
        self.assertIn("ai::PullLeadReleaseReason(", fn)
        self.assertLess(fn.index("PullWindowExpired("), fn.index("PullLeadReleaseReason("))
        # The timer expiry is the ceiling (release at once), not a gate: a
        # lead-keyed window asks the lead rule while it still runs, so the
        # early release (pack on the tank at the join delay) and the held-bot
        # rescue (a held bot is hit -> release at once) fire before the cap.
        expiry = fn.index("if (ai->PullWindowExpired(")
        self.assertIn("return true;", fn[expiry:fn.index(";", expiry) + 30])
        lead = fn.index("if (!ai->PullWindowLeadKeyed())")
        self.assertIn("return false;", fn[lead:fn.index(";", lead) + 30])
        self.assertLess(expiry, lead)
        self.assertLess(lead, fn.index("ai::PullLeadReleaseReason("))

    def test_run_leader_party_hold_is_lead_keyed(self):
        fn = body(COMMANDS, "void PausePartyDpsForPull(")
        self.assertIn("StartPullWindow(static_cast<uint8>(windowSeconds), context.runLeader,", fn)

    def test_landing_marks_only_where_the_spec_says(self):
        self.assertIn("RestampPullParty(bot, 0, false, !strategy->IsCommandPullback())", PULL_ACTIONS)
        self.assertIn("RestampPullParty(bot, strategy->GetCommandJoinDelay(), true, true)", PULL_ACTIONS)

    def test_chat_pullback_hold_stays_plain_timer(self):
        fn = body(PULL_ACTIONS, "void ai::PausePartyPullHold(Player* tank, uint32 joinDelaySeconds)")
        start = fn.index("StartPullWindow(")
        call = fn[start:fn.index(";", start)]
        self.assertNotIn(",", call)


class PullLeadReleasePolicy(unittest.TestCase):
    # The three brief cases, straight through the shipped rule: a tick calls
    # PullLeadReleases once the window runs, and the wide-window expiry
    # releases a pullback that never landed.
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_release_rule_cases(self):
        policy = (ROOT / "ai/playerbot/PullJoinWindowPolicy.h").read_text()
        harness = r"""
#include <cassert>
#include <cstdio>
"""
        harness += policy + r"""
int main()
{
    // Landed, every mob on the tank at the join delay: release before the cap.
    ai::PullLeadFacts landed;
    landed.landed = true; landed.elapsedSeconds = 5; landed.minSeconds = 5;
    landed.tankAlive = true; landed.packUnits = 3; landed.packOnTank = 3;
    assert(ai::PullLeadReleases(landed));
    // Same pack, joiners still arriving: hold.
    landed.packOnTank = 2;
    assert(!ai::PullLeadReleases(landed));
    // A held DPS is hit: release at once, even before the join delay.
    ai::PullLeadFacts hit = landed;
    hit.elapsedSeconds = 1; hit.botIsVictim = true;
    assert(ai::PullLeadReleases(hit));
    // Never landed (the pullback tank is still out): hold past the wide
    // window, then the window expiry releases it. The window's own Expired
    // is that ceiling: 15 s return + 5 s join after the command.
    ai::PullLeadFacts lost;
    lost.landed = false; lost.elapsedSeconds = 30;
    lost.tankAlive = true;
    assert(!ai::PullLeadReleases(lost));
    ai::PullJoinWindow window;
    window.Start(1000, 0, 0, true, 5);
    assert(window.Running());
    assert(!window.Expired(1000 + 19, 20));
    assert(window.Expired(1000 + 20, 20));
    std::puts("policy ok");
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="pull-lead-release-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


class BotOnlyRunHoldWiring(unittest.TestCase):
    def test_bot_only_run_parties_keep_the_hold(self):
        combat = (ROOT / "ai/playerbot/strategy/generic/CombatStrategy.cpp").read_text()
        fn = body(combat, "bool WaitForAttackStrategy::ShouldWait(PlayerbotAI* ai)")
        # LEAD DECISION (bot-only bound run): the DPS hold applies ONLY to a
        # run-leader pull window (the pull hold), never to a bot's own stored
        # "wait for attack" strategy at every fight start.
        self.assertIn("IsInRunningDungeonRun(bot)", fn)
        self.assertIn("HasPullWindow()", fn)

    def test_player_stored_wait_for_attack_ignores_the_run_gate(self):
        combat = (ROOT / "ai/playerbot/strategy/generic/CombatStrategy.cpp").read_text()
        fn = body(combat, "bool WaitForAttackStrategy::ShouldWait(PlayerbotAI* ai)")
        self.assertIn("HasRealPlayerMaster()", fn)
        run = fn.index("IsInRunningDungeonRun(bot)")
        window = fn.index("HasPullWindow()")
        self.assertLess(run, window)

class FightStartHoldWiring(unittest.TestCase):
    def test_combat_entry_opens_the_fight_start_hold(self):
        fn = body(STATE_ACTIONS, "bool SetCombatStateAction::Execute(Event& event)")
        self.assertIn("StartFightStartHold()", fn)
        self.assertIn("OnCombatStarted()", fn)
        self.assertIn("if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT))", fn)
        self.assertLess(fn.index("StartFightStartHold()"), fn.index("OnCombatStarted()"))

    def test_own_attack_never_opens_a_hold(self):
        fn = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::OnCombatStarted()")
        self.assertNotIn("StartFightStartHold", fn)

    def test_fight_start_hold_builds_the_gate_and_lands_at_once(self):
        fn = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::StartFightStartHold()")
        self.assertIn("ai::FightStartHoldSkipReason(", fn)
        self.assertIn("kFightStartHoldSeconds", fn)
        self.assertIn("StartPullWindow(ai::kFightStartHoldSeconds, true, ai::kFightStartMinSeconds, true)", fn)
        self.assertIn("RestampPullWindow(true)", fn)

    def test_pack_counts_mobs_beside_the_held_bot(self):
        fn = body(PULL_TRIGGERS, "bool PullHoldExpiredTrigger::IsActive()")
        self.assertIn("bot->IsWithinDistInMap(unit, ai::kPullLeadPackYd)", fn)
        self.assertIn("PullLeadReleaseReason(", fn)
        self.assertLess(fn.index("PullWindowExpired("), fn.index("PullLeadReleaseReason("))

    def test_release_logs_its_reason(self):
        fn = body(PULL_ACTIONS, "bool ReleasePullHoldAction::Execute(Event& event)")
        self.assertIn("EndPullWindow(", fn)
        self.assertIn("PullReleaseReason()", fn)
        end = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::EndPullWindow(")
        self.assertIn("pull-release", end)
        self.assertIn("reason=", end)


class FightStartGateIgnoresMaster(unittest.TestCase):
    def test_fight_start_gate_ignores_the_master(self):
        fn = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::StartFightStartHold()")
        self.assertNotIn("HasRealPlayerMaster", fn)
        self.assertNotIn("GetMaster", fn)
        self.assertIn("FillFightStartGroupFacts(", fn)
        self.assertIn("IsRealPlayer(member)", fn)
        self.assertIn("FightStartHoldSkipReason(", fn)
        self.assertIn("fight-start-hold open", fn)
        self.assertIn("fight-start-hold skip", fn)
        self.assertIn("facts.runningRun)", fn)
        self.assertLess(fn.index("facts.runningRun)"), fn.index("fight-start-hold skip"))

    def test_fight_start_early_return_does_not_hide_the_window(self):
        fn = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::StartFightStartHold()")
        first = fn.index("return;")
        window = fn.index("facts.pullWindow =")
        self.assertLess(first, window)
        early = fn[:first]
        self.assertNotIn("HasPullWindow", early)
        window_line = fn[fn.index("facts.pullWindow ="):fn.index("facts.pullWindow =") + 80]
        self.assertIn("HasPullWindow()", window_line)

    def test_policy_has_no_master_fact(self):
        policy = (ROOT / "ai/playerbot/PullJoinWindowPolicy.h").read_text()
        self.assertNotIn("realPlayerMaster", policy)


if __name__ == "__main__":
    unittest.main()
