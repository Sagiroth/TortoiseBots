"""The run leader's walk of one bot (`walk <bot> x y z`): the glue (Shadowfang Keep r25 slot 5 step 39, r26 slot 7 step 46).

The pure rules (the straggler ladder: StragglerLadderTick, StragglerLadderPending, UpdateStraggle; the walk back of
a rogue whose Sap failed in SapWait) are tested in tools/test_dungeon_run_policy.cpp; this pins where
commands/BotCommands.cpp obeys them, since that file only builds against the core:
- IssueRunOrder hands a `walk ` order to RunBotWalk, not to `.bot cmd`;
- RunBotWalk checks the bot (a bot of the commanded group, alive, out of combat, on the lead bot's map), reads its
  own navmesh path to the point, and only then puts it on stay at the point and walks it there by path;
- SyncRunWalkers lets a walker go: formed up on the lead bot in a fight or once its run is not running, only
  dropped once the party's next move took it off stay; it runs every tick, also with no run left;
- a player's order to the party drops the list without forming the bots up.
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


class StragglerWalkWiringTest(unittest.TestCase):
    def test_issue_run_order_routes_walk_to_the_glue(self):
        issue = body(COMMANDS, "bool IssueRunOrder(Player* commander", "HandleCommanderOrder(handler.get(), text.c_str());")
        self.assertIn('if (text.compare(0, 5, "walk ") == 0)\n        return RunBotWalk(commander, text, answer);', issue)

    def test_policy_order_spelling_matches_the_parser(self):
        self.assertIn('return "walk " + bot + " " + Num(x) + " " + Num(y) + " " + Num(z);', POLICY)
        walk = body(COMMANDS, "bool RunBotWalk(Player* commander, std::string const& order, std::string& answer)\n{",
            "bool RunIslandEscape(Player* commander, std::string const& order, std::string& answer)\n{")
        self.assertIn('std::sscanf(order.c_str(), "walk %63s %f %f %f", name, &x, &y, &z) != 4', walk)

    def test_walk_checks_before_it_touches_the_bot(self):
        walk = body(COMMANDS, "bool RunBotWalk(Player* commander, std::string const& order, std::string& answer)\n{",
            "bool RunIslandEscape(Player* commander, std::string const& order, std::string& answer)\n{")
        self.assertIn("for (Player* member : CommandedBots(group, lead))", walk)
        self.assertIn("member != lead", walk)
        self.assertIn("!bot->IsAlive() || bot->IsInCombat() || bot->GetMapId() != lead->GetMapId()", walk)
        self.assertIn("PathFinder path(bot);", walk)
        self.assertIn("path.calculate(x, y, z, false);", walk)
        self.assertIn("ai::WorldPosition::PermissivePathWalkable(permissive.getPath())", walk)
        self.assertIn("(active->getPathType() & PATHFIND_NOPATH) || short_ > 5.0f", walk)
        checks = walk.index("PATHFIND_NOPATH")
        touch = walk.index('ai->SetMovementStrategy("stay");')
        self.assertLess(checks, touch)
        self.assertLess(walk.index("bot->IsInCombat()"), touch)

    def test_walk_holds_the_bot_at_the_point_on_its_own_path(self):
        walk = body(COMMANDS, "bool RunBotWalk(Player* commander, std::string const& order, std::string& answer)\n{",
            "bool RunIslandEscape(Player* commander, std::string const& order, std::string& answer)\n{")
        for line in (
            "ai->DropQueuedMovementOrders();",
            'ai->SetMovementStrategy("stay");',
            "SetStayAnchor(ai, x, y, z, bot->GetMapId());",
            'ExecuteQuietAction(ai, "return to stay position", ai::Event("return to stay position", "stay", commander));',
            "RunWalkers()[bot->GetObjectGuid().GetRawValue()] = commanderRaw;",
        ):
            self.assertIn(line, walk)

    def test_walk_refuses_a_detour_a_pack_or_a_hostile_before_it_touches_the_bot(self):
        # Codex adversarial review of 95f66dd8, FIX FIRST 1 (1).
        walk = body(COMMANDS, "bool RunBotWalk(Player* commander, std::string const& order, std::string& answer)\n{",
            "bool RunIslandEscape(Player* commander, std::string const& order, std::string& answer)\n{")
        self.assertIn("TortoiseBots::DungeonRun::BotWalkRefusal(points, straight, pathYd,", walk)
        self.assertIn("hasRun ? run->second.run.plan : nullptr, hasRun ? run->second.run.step : 0,", walk)
        self.assertIn("hasRun ? run->second.run.notCleared : std::unordered_set<std::size_t>(), hostiles);", walk)
        self.assertIn("float const pathYd = active->Length();", walk)
        self.assertIn("for (Vector3 const& v : active->getPath())", walk)
        self.assertIn("if (unit->IsAlive() && !unit->IsInCombat())", walk)
        self.assertIn("HostileCreaturesAround(bot, std::min(pathYd, TortoiseBots::DungeonRun::kBotWalkMaxPathYd) +", walk)
        self.assertIn('answer = "unreachable: " + std::string(name) + ": " + refusal;\n            return false;', walk)
        self.assertLess(walk.index("BotWalkRefusal("), walk.index('ai->SetMovementStrategy("stay");'))

    def test_sync_lets_walkers_go(self):
        sync = body(COMMANDS, "void SyncRunWalkers()\n{", "// Once a second: the healers of every running run")
        self.assertIn('found && ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT)', sync)
        self.assertIn("run->second.run.state == TortoiseBots::DungeonRun::State::Running", sync)
        self.assertIn("TortoiseBots::DungeonRun::WalkerVerdict(found,", sync)
        self.assertIn("found && bot->IsInCombat(), running, leadAvailable, inGroup,", sync)
        self.assertIn("found && leadAvailable && bot->GetMapId() == lead->GetMapId());", sync)
        # FIX FIRST 1 (2): membership in the lead bot's commanded group, a lead bot from the live binding.
        self.assertIn("RemoteCommander::Binding const* binding = CommanderBindings().FindByCommander(it->second);", sync)
        self.assertIn("for (Player* member : CommandedBots(group, lead))", sync)
        self.assertIn("inGroup = inGroup || member == bot;", sync)
        release = body(sync, "if (fate == WalkerFate::Release)", "it = RunWalkers().erase(it);")
        self.assertIn('ai->SetMovementStrategy("follow");', release)
        self.assertLess(release.index('ai->SetMovementStrategy("follow");'), release.index("FormUpOnLead("))
        self.assertIn("if (inGroup && bot->GetMapId() == lead->GetMapId())", release)
        self.assertIn("FormUpOnLead(commander, lead, std::vector<Player*>{ bot }, false);", release)
        update = body(COMMANDS, "static void UpdateDungeonRuns(uint32 diff)", "void PauseDungeonRunForPlayer(")
        # Before the early return for no runs: a stopped last run's walker follows again.
        self.assertLess(update.index("SyncRunWalkers();"), update.index("if (DungeonRuns().empty())\n    {"))

    def test_a_players_order_drops_the_walkers_untouched(self):
        pause = body(COMMANDS, "void PauseDungeonRunForPlayer(", "static bool RebuildBossSpawnCache(")
        self.assertIn("walker = walker->second == commanderRaw ? RunWalkers().erase(walker) : std::next(walker);", pause)
        drop = body(pause, "for (auto walker = RunWalkers().begin();", "SendRunLine(")
        self.assertNotIn("FormUpOnLead", drop)

    def test_policy_hooks_the_ladder_into_every_gather_wait(self):
        self.assertEqual(POLICY.count("else if (StragglerLadderTick(r, f, order))"), 3)
        self.assertIn("if (StragglerLadderTick(r, f, order))\n                {\n                    r.note = \"regroup: \" + r.note;", POLICY)
        self.assertIn("SummonStraggler(r, f, order) || HeldStragglerWait(r, f) || StragglerLadderPending(r, f)", POLICY)
        ceiling = body(POLICY, "inline void GatherCeiling(", "// A regroup's gather: a living bot far")
        self.assertLess(ceiling.index("StragglerLadderPending(r, f)"), ceiling.index("Pause(r, pauseText);"))
        self.assertIn("UpdateStraggle(r, f, dt);", body(POLICY, "inline std::string TickBody(", "bool const allDead"))
        refused = body(POLICY, "inline void OrderRefused(Run& r", "\ninline ")
        self.assertIn("if (StragglerWalkRefused(r, order, answer))", refused)


class StragglerLivePathWiring(unittest.TestCase):
    """A patrol is pulled from its loop point at its live position, not at the point."""

    def test_attack_read_ends_at_the_units_live_position(self):
        fn = body(COMMANDS, "void ReadStragglerWay(Player* lead", "// A leg toward (x,y,z)")
        self.assertIn("f.stragglerPathToUnit = run.stragglerAttackPath;", fn)
        self.assertIn("lead->GetMap()->GetUnit(::ObjectGuid(wanted))", fn)
        self.assertIn("targetX = unit->GetPositionX();", fn)
        self.assertIn("DirectNavPath(tank, targetX, targetY, targetZ, way, f.dryWalk && DryAt(tank, targetX, targetY, targetZ))", fn)

    def test_the_policy_waits_at_the_point_and_matches_the_read(self):
        self.assertIn("constexpr float kStragglerPatrolReachYd", POLICY)
        self.assertIn("f.stragglerPathToUnit != r.stragglerAttackPath", POLICY)
        self.assertIn("r.stragglerAttackPath = loopPoint; // to the patrol's live position, not the loop point", POLICY)


if __name__ == "__main__":
    unittest.main()
