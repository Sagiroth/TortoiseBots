"""Chases stop at ledges, and a stranded lead bot is teleported back (owner decisions 2026-10-23).

Sunken Temple r26 slot 6b step 233: the lead bot went from the z -172.9 floor 13 yd down onto an island
(z -184..-191) with no navmesh route back; every resume paused again ('the straight hop off the navmesh
island was not made (no-spot ...)', 'walk back: the point of step 234 refused (unreachable)'). The pure
rules are tested in tools/test_runner_finish_policy.cpp (RunnerOverLedge, RunnerStaysLetGo) and
tools/test_dungeon_run_policy.cpp (ShotStandOnPath's ledge cut, StrandedLead, StrandedTeleportTick); this
pins where the glue calls them:
- a bot lets a running unit more than 6 yd below its floor go on every pick path: the attackers list
  (AttackersValue::IgnoreTarget) and PossibleAttackTargetsValue::IsValid (the tank's victim, raid marks,
  explicit orders and the current target's validity), from its own "runners let go" book;
- the runner finisher fills the drop fact the policy reads;
- the run's "rescue ..." order never goes out as a `cmd`: IssueRunOrder hands it to RunStrandedTeleport,
  whose sscanf format matches the order the policy writes; every check (moved, fight) comes before the
  first change to the lead bot, and the teleport is the core's NearTeleportTo;
- the walk back sends the teleport before anything else; the firing stand keeps the bots' ledge height.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
ATTACKERS = (ROOT / "ai/playerbot/strategy/values/AttackersValue.cpp").read_text()
POSSIBLE = (ROOT / "ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp").read_text()
DPS = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()
TARGETS = (ROOT / "ai/playerbot/strategy/values/TargetValue.h").read_text()
ATTACK_ACTION = (ROOT / "ai/playerbot/strategy/actions/AttackAction.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def at(text, needle):
    i = text.find(needle)
    if i < 0:
        raise AssertionError("missing: " + needle)
    return i


class BotsLetRunnersGoAtLedgesTest(unittest.TestCase):
    def test_the_book_is_registered_with_its_type(self):
        at(CONTEXT, 'creators["runners let go"] = [](PlayerbotAI* ai) { return new RunnersLetGoValue(ai); };')
        at(TARGETS, 'ManualSetValue<std::map<ObjectGuid, RunnerLedgeEntry>& >(ai, data, "runners let go")')
        reads = re.findall(r'GetValue<(std::map<[^>]+>&)>\("runners let go"\)', ATTACKERS + ATTACK_ACTION)
        self.assertEqual(len(reads), 2)
        for typ in reads:
            self.assertIn(typ.replace(" ", ""), ("std::map<ObjectGuid,ai::RunnerLedgeEntry>&",
                                                 "std::map<ObjectGuid,RunnerLedgeEntry>&"))

    def test_let_go_scope_and_order(self):
        f = body(ATTACKERS, "bool AttackersValue::RunnerLetGoAtLedge(Unit* target, Player* bot)\n{")
        purge = at(f, "ai::PurgeRunnerBook(book, now);")
        target = at(f, "if (!target || target->IsPlayer()")
        explicit = at(f, 'context->GetValue<ObjectGuid>("explicit attack target")->Get() == guid')
        clear = at(f, "book.erase(guid);\n        return false;")
        stays = at(f, "ai::RunnerStaysLetGo(true, now, entry->second.letGoUntil, facts.dropYd)")
        filters = at(f, "if (!ai::RunnerOverLedge(facts) || !TortoiseBots::RunReleaseHold::HeldByRun(bot))")
        read = at(f, "ReadRunnerWay(target, bot, complete, cliff);")
        verdict = at(f, "if (!ai::LetRunnerGoAtLedge(true, false, facts, true, cliff))")
        mark = at(f, "entry->second.letGoUntil = now + ai::kRunnerLetGoMs;")
        # Purge on every access before any target-specific return; the explicit order before any refusal;
        # the cheap filters (height, run binding) before the one navmesh path.
        self.assertTrue(purge < target < explicit < clear < stays < filters < read < verdict < mark)
        at(f, "cliff = cliff || !complete;")
        at(f, "read.readUntil = now + ai::kRunnerLedgeReadMs;")
        self.assertEqual(f.count("ReadRunnerWay("), 1)

    def test_the_way_is_read_from_the_navmesh(self):
        f = body(ATTACKERS, "static void ReadRunnerWay(Unit* target, Player* bot, bool& complete, bool& cliff)\n{")
        at(f, "path.calculate(tx, ty, tz, false);")
        at(f, "complete = path.getPathType() == PATHFIND_NORMAL;")
        at(f, "cliff = ai::PathHasCliff(points);")
        at(f, "points.push_back(front);")

    def test_an_explicit_order_clears_the_let_go(self):
        f = body(ATTACK_ACTION, "void CommitExplicitAttackTarget(PlayerbotAI* ai, ObjectGuid guid)\n{")
        at(f, 'GetValue<std::map<ObjectGuid, RunnerLedgeEntry>&>("runners let go")->Get().erase(guid);')

    def test_the_run_end_is_read_only_from_a_live_spline(self):
        f = body(ATTACKERS, "float AttackersValue::RunnerDropYd(Unit* target, Player* bot)\n{")
        at(f, "target->movespline->Initialized() && !target->movespline->Finalized()")
        at(f, "ai::RunnerDropYd(bot->GetPositionZ(), target->GetPositionZ(), runEndKnown, runEndZ)")

    def test_every_pick_path_asks(self):
        ignore = body(ATTACKERS, "bool AttackersValue::IgnoreTarget(Unit* target, Player* playerToCheckAgainst)\n{")
        ask = at(ignore, "if (RunnerLetGoAtLedge(target, playerToCheckAgainst))\n        return true;")
        self.assertLess(ask, at(ignore, '"unreachable targets"'))
        valid = body(POSSIBLE, "bool PossibleAttackTargetsValue::IsValid(")
        ledge = at(valid, "if (AttackersValue::RunnerLetGoAtLedge(target, player))\n        return false;")
        # Before the attackers check (skipped for explicit orders) and every early acceptance.
        self.assertLess(ledge, at(valid, "if (checkAttackerValid && !AttackersValue::IsValid(target, player))"))
        self.assertLess(ledge, at(valid, 'PAI_VALUE(ObjectGuid, "attack target")'))

    def test_the_runner_finisher_fills_the_drop(self):
        f = body(DPS, "Unit* FinishRunnerTarget(")
        running = at(f, "facts.running = AttackersValue::IsRunningMob(unit);")
        drop = at(f, "facts.dropYd = AttackersValue::RunnerDropYd(unit, bot);")
        letgo = at(f, "facts.letGo = AttackersValue::RunnerLetGoAtLedge(unit, bot);")
        should = at(f, "ai::ShouldFinishRunner(facts, mobChaser)")
        self.assertTrue(running < drop < letgo < should)
        self.assertNotIn("bool IsRunningMob(Unit* unit)", DPS)  # one definition, AttackersValue's


class StrandedLeadTeleportWiringTest(unittest.TestCase):
    def setUp(self):
        self.glue = body(COMMANDS, "bool RunStrandedTeleport(Player* commander, std::string const& order, std::string& answer)\n{")

    def test_issue_run_order_hands_the_rescue_to_the_glue(self):
        f = body(COMMANDS, "bool IssueRunOrder(")
        route = at(f, 'if (text.compare(0, 7, "rescue ") == 0)\n        return RunStrandedTeleport(commander, text, answer);')
        self.assertLess(route, at(f, "HandleCommanderOrder(handler.get(), text.c_str());"))

    def test_order_text_matches_the_policy(self):
        at(self.glue, '"rescue from %f %f %f to %f %f %f"')
        p = body(POLICY, "inline bool StrandedTeleportTick(")
        send = at(p, 'order = "rescue from " + now + " to " + Num(p.x) + " " + Num(p.y) + " " + Num(p.z);')
        # The spot is this tick's facts, read right before the send (a lead bot that moved on the island).
        fresh = at(p, "r.strandX = f.leadX;\n    r.strandY = f.leadY;\n    r.strandZ = f.leadZ;")
        self.assertLess(fresh, at(p, 'std::string const now = Num(r.strandX) + " " + Num(r.strandY) + " " + Num(r.strandZ);'))
        self.assertLess(fresh, send)
        # The policy's refusal handling reads the same verb.
        refused = body(POLICY, "inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)\n{")
        at(refused, 'if (order.compare(0, 7, "rescue ") == 0)')

    def test_checks_come_before_the_first_change_to_the_lead_bot(self):
        f = self.glue
        alive = at(f, "if (!leadAi || !lead->IsAlive() || lead->IsTaxiFlying() || lead->IsBeingTeleported())")
        moved = at(f, "> kEscapeMovedYd)")
        fight = at(f, "bool fight = lead->IsInCombat();")
        wait = at(f, 'answer = "wait: a party bot is in combat";')
        teleport = at(f, "if (!lead->NearTeleportTo(x, y, ground + 0.5f, lead->GetOrientation()))")
        failed = at(f, 'answer = "failed: the near teleport was refused";\n        return false;')
        supersede = at(f, "SupersedeCommanderTasks(commander->GetObjectGuid().GetRawValue(), 0, RemoteCommander::LaterOrder::Move);")
        drop = at(f, "leadAi->DropQueuedMovementOrders();")
        hold = at(f, "PlayerConvenience::Instance().NoteHoldOrder(lead->GetObjectGuid());")
        reset = at(f, "leadAi->Reset();")
        stay = at(f, 'leadAi->SetMovementStrategy("stay");')
        anchor = at(f, "SetStayAnchor(leadAi, x, y, ground, lead->GetMapId());")
        clear = at(f, "lead->GetMotionMaster()->Clear();")
        # Every check, then the teleport; nothing on the lead bot changes before it was accepted.
        self.assertTrue(alive < moved < fight < wait < teleport < failed < supersede < drop < hold < reset < stay <
                        anchor < clear)
        self.assertEqual(f.count("NearTeleportTo("), 1)
        self.assertEqual(f.count("leadAi->Reset()"), 1)
        self.assertRegex(f, r"> kEscapeMovedYd\)\s*\{\s*answer = \"wait: ")

    def test_walk_back_sends_it_first(self):
        f = body(POLICY, "inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)\n{")
        fallen = at(f, "FallenFirst(r, f)")
        strand = at(f, "if (StrandedTeleportTick(r, f, dt, order))\n        return true;")
        self.assertLess(fallen, strand)
        self.assertLess(strand, at(f, "PickPull(r, f.hopUnits"))

    def test_the_walk_back_refusal_takes_it_before_passing_the_point(self):
        refused = body(POLICY, "inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)\n{")
        strand = at(refused, "if (IsPathRefusal(answer) && StrandedLead(r, answer))")
        passed = at(refused, "PassWalkBackPoint(r, r.leadLastX, r.leadLastY, r.leadLastZ, point + \" refused (\" + answer + \")\"")
        self.assertLess(strand, passed)

    def test_once_a_step_survives_reset_step(self):
        reset = body(POLICY, "inline void ResetStep(Run& r)\n{")
        at(reset, "r.strandDue = false;")
        self.assertNotIn("strandStep", reset)

    def test_the_firing_stand_keeps_the_bots_ledge_height(self):
        f = body(COMMANDS, "void ReadPullStand(")
        guard = at(f, "static_assert(kStandLedgeDropYd == ai::kRunnerLedgeDropYd,")
        self.assertLess(guard, at(f, "ShotStandOnPathCleared(points, goal, f.leadShotYd - kShotMarginYd, sees,"))


if __name__ == "__main__":
    unittest.main()
