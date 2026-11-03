"""The lead bot in another part of the instance: the glue (Scarlet Monastery r20-19z39 slot 7).

The pure rules (LeadInOtherPart, PauseOtherPart, HoldStandsNow, the near-wipe
hold, no summon or island hop toward such a lead bot) are tested in
tools/test_dungeon_run_policy.cpp; this pins where commands/BotCommands.cpp
feeds and obeys them, since that file only builds against the core:
- ReadRunFacts gives the policy the instance's doors (Facts::doors): the
  destination of every area trigger onto the plan's map, from a per-map cache;
- SyncRunHold keeps the held bots on stay while HoldStandsNow (running, or the
  pause for a lead bot in another part), and the paused tick does not release
  them then (their follow would walk them out through the instance's exits).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    j = text.index(end, i + len(start))
    return text[i:j]


class LeadOtherPartWiringTest(unittest.TestCase):
    def test_doors_are_the_trigger_destinations_onto_the_map(self):
        doors = body(COMMANDS, "InstanceDoors(uint32 mapId)", "void ReadRunFacts(")
        self.assertIn("at->destination.mapId != mapId", doors)
        for axis in "xyz":
            self.assertIn(f"door.{axis} = at->destination.{axis};", doors)
        self.assertIn("static std::unordered_map<uint32, std::vector<TortoiseBots::DungeonRun::DoorFact>> cache;", doors)

    def test_read_run_facts_fills_the_doors_for_a_living_lead(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "f.leadX = lead->GetPositionX();")
        self.assertIn("f.doors = InstanceDoors(slot.run.plan->map);", facts)
        self.assertIn("if (f.leadAlive)", facts)

    def test_the_hold_stands_while_paused_for_it(self):
        sync = body(COMMANDS, "void SyncRunHold(", "void SyncHoldStands(")
        self.assertIn("TortoiseBots::DungeonRun::HoldStandsNow(slot.run)", sync)
        self.assertNotIn("slot.run.state == TortoiseBots::DungeonRun::State::Running &&", sync)
        tick = body(COMMANDS, "void TickDungeonRun(", "Player* lead = FindCharacter(binding->leadGuid);\n    Group* group")
        paused = body(tick, "if (slot.run.state != TortoiseBots::DungeonRun::State::Running || !commander)", "return;")
        self.assertIn("if (!(commander && TortoiseBots::DungeonRun::HoldStandsNow(slot.run)))", paused)
        self.assertIn("ReleaseRunHold(commander, binding->leadGuid, slot);", paused)

    def test_policy_pause_clears_the_flag(self):
        pause = body(POLICY, "inline void Pause(Run& r, const std::string& why)", "inline bool HoldStandsNow(")
        self.assertIn("r.holdWhilePaused = false;", pause)


    def test_resume_is_refused_while_the_lead_is_still_away(self):
        # SM-REVIVE-PATH-2026-10-23 fix 5: `.bot run resume` (the agent's dungeon_resume) asks
        # ResumeChecked with the lead bot as it stands, and a refusal answers ACTION_ERR and keeps the pause.
        resume = body(COMMANDS, 'else if (verb == "resume")', "else // skip")
        self.assertNotIn("done = Resume(slot.run);", resume)
        self.assertIn("done = ResumeChecked(slot.run, spot, refusal, door);", resume)
        self.assertIn("Player* lead = FindCharacter(binding->leadGuid)", resume)
        self.assertIn("if (lead->IsInWorld())", resume)
        self.assertIn("spot.read = true;", resume)
        self.assertIn("spot.alive = lead->IsAlive();", resume)
        self.assertIn("spot.onPlanMap = lead->GetMapId() == slot.run.plan->map;", resume)
        for axis in "XYZ":
            self.assertIn(f"spot.{axis.lower()} = lead->GetPosition{axis}();", resume)
        refused = body(resume, "if (!refusal.empty())", "}")
        self.assertIn('SendActionError(handler, intent, "lead-other-part", refusal.c_str());', refused)
        self.assertIn("return true;", refused)

    def test_policy_pause_marks_and_resume_clears_the_other_part_pause(self):
        other = body(POLICY, "inline void PauseOtherPart(", "\n}\n")
        self.assertIn("r.otherPartPaused = true;", other)
        self.assertGreater(other.index("r.otherPartPaused = true;"), other.index("Pause(r, "))
        self.assertIn("r.otherPartPaused = false;", body(POLICY, "inline void Pause(Run& r, const std::string& why)", "\n}\n"))
        self.assertIn("r.otherPartPaused = false;", body(POLICY, "inline bool Resume(Run& r)", "\n}\n"))

    def test_revived_in_another_part_is_rescued(self):
        # Dire Maul West r36 slot D: the rescue teleport before the pause, at every place the pause was the answer;
        # r39/r40 slot 6: a bot still dead is waited for (the regroup), not paused on.
        why = body(POLICY, "inline OtherPartBlock OtherPartRescueWhy(const Run& r, const Facts& f)\n{", "\n}\n")
        order = ["if (!f.players.empty())", "if (!f.walkBackRescue)",
                 "if (r.otherPartRescueStep == r.step && r.otherPartRescues >= kOtherPartRescuesPerStep)",
                 "if (!RescueAllowed(r, f))", "if (OtherPartSafePoint(r, f) == static_cast<std::size_t>(-1))",
                 "if (f.dead > 0)"]
        for first, second in zip(order, order[1:]):
            self.assertLess(why.index(first), why.index(second))
        point = body(POLICY, "inline std::size_t OtherPartSafePoint(const Run& r, const Facts& f)", "\n}\n")
        self.assertIn("std::size_t const last = LastReachedBefore(r, r.step);", point)
        self.assertIn("return SafePointAtOrBefore(r, f.team, last, &f);", point)
        target = body(POLICY, "inline std::size_t OtherPartRescueTarget(const Run& r, const Facts& f)", "\n}\n")
        self.assertIn("if (OtherPartRescueWhy(r, f) != OtherPartBlock::None)", target)
        rescue = body(POLICY, "inline bool OtherPartRescue(Run& r, const Facts& f)\n{", "\n}\n")
        self.assertLess(rescue.index("RescueDue(r, f, target);"), rescue.index("r.strandBotOnly = true;"))
        self.assertIn("++r.otherPartRescues;", rescue)
        revived = body(POLICY, "inline bool LeadRevivedAway(Run& r, const Facts& f)", "\n}\n")
        other = body(revived, "if (LeadInOtherPart(r, f))", "if (r.autoRegroups >= kAutoRegroupsPerStep)")
        self.assertNotIn("r.autoRegroups < kAutoRegroupsPerStep", other)
        self.assertLess(other.index("RearmRescueAfterDeath(r);"), other.index("OtherPartRescueWhy(r, f)"))
        self.assertIn("if (why == OtherPartBlock::None || why == OtherPartBlock::Dead)", other)
        self.assertLess(other.index("++r.autoRegroups;"), other.index("OtherPartRescue(r, f)"))
        self.assertLess(other.index("ResetStep(r);"), other.index("OtherPartRescue(r, f)"))
        self.assertLess(other.index("OtherPartRescue(r, f)"), other.index("BeginRegroup(r);"))
        self.assertLess(other.index("BeginRegroup(r);"), other.index("PauseOtherPart(r, f, why);"))
        regroup = body(POLICY, "if (r.regroup && !runBackWait && !f.anyInCombat)", "if (r.holdDue && f.leadAlive)")
        site = body(regroup, "if (LeadInOtherPart(r, f))", "PauseOtherPart(r, f, why);")
        self.assertIn("if (why == OtherPartBlock::None && OtherPartRescue(r, f))", site)
        for line in ("r.regroup = false;", "r.waitFor.clear();", "r.walkBackDue = false;", "r.hold.clear();"):
            self.assertIn(line, site)
        wait = body(site, "if (why == OtherPartBlock::Dead && r.regroupMs <= kRegroupReviveMs + r.reviveBaseMs)", "return order;")
        self.assertIn('order = "revive party";', wait)
        island = body(POLICY, "inline void IslandEscape(", "std::string const spot =")
        self.assertLess(island.index("OtherPartRescue(r, f)"), island.index("PauseOtherPart(r, f, why);"))
        self.assertEqual(POLICY.count("PauseOtherPart(r, f, why);"), 3)
        self.assertNotIn("PauseOtherPart(r, f);", POLICY)

    def test_resume_runs_the_rescue_for_a_passing_reason(self):
        # r39/r40 slot 6: the resume of a pause whose reason passes (a dead bot, the cap, the latch, a fight) runs the
        # rescue instead of the lead-other-part refusal; the other reasons keep the refusal.
        checked = body(POLICY, "inline bool ResumeChecked(Run& r, const LeadSpot& lead, std::string& refusal,", "\n}\n")
        rescue = body(checked, "if (r.otherPartResumeRescue)", "refusal = ")
        self.assertLess(rescue.index("RearmRescueAfterDeath(r);"), rescue.index("Resume(r)"))
        resumes = body(POLICY, "inline bool OtherPartResumeRescues(OtherPartBlock why)", "\n}\n")
        for kind in ("Dead", "Cap", "Used", "Busy"):
            self.assertIn(f"OtherPartBlock::{kind}", resumes)
        for kind in ("Player", "Off", "Tries", "NoPoint", "None"):
            self.assertNotIn(f"OtherPartBlock::{kind}", resumes)
        pause = body(POLICY, "inline void PauseOtherPart(", "\n}\n")
        self.assertGreater(pause.index("r.otherPartResumeRescue = resumeRescues;"), pause.index("Pause(r, "))
        self.assertIn("r.otherPartResumeRescue = false;", body(POLICY, "inline void Pause(Run& r, const std::string& why)", "\n}\n"))
        self.assertIn("r.otherPartRescues = 0;", body(POLICY, "inline bool Resume(Run& r)", "\n}\n"))

if __name__ == "__main__":
    unittest.main()
