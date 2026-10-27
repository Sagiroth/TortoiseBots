"""The glue of the player-led (hybrid) dungeon run (runtime/PlayerLedPolicy.h, 2026-10-24).

The policy (tools/test_player_led_policy.cpp) decides; these checks pin that commands/BotCommands.cpp and the config
hand it the facts and act on its answers. S1: `.bot run start ... player [tank <Bot>]` makes its own binding of the
commander's party (the commander stays leader, no follow target changes, no form-up on the tank), the binding's state
reads whether the commander leads, a refused start releases the binding again, and a stopped, done or switched-off
player-led run releases its binding. `.bot run mode` names the mode.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PlayerLedBindWiringTest(unittest.TestCase):
    def test_keys_default_off_and_hot_reload(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn('dungeonRunPlayerLed = config.GetBoolDefault("AiPlayerbot.DungeonRunPlayerLed", false);', keys)
        self.assertIn('config.GetFloatDefault("AiPlayerbot.DungeonRunPlayerLedPullYd", 40.0f)', keys)
        self.assertIn("AiPlayerbot.DungeonRunPlayerLed = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunPlayerLedPullYd = 40", TEMPLATE)

    def test_binding_state_reads_the_commanders_lead(self):
        state = strip_comments(body(COMMANDS, "RemoteCommander::BindingState ReadBindingState(", "\n}\n"))
        self.assertIn("state.commanderIsLeader = group->IsLeader(::ObjectGuid(binding.commanderGuid));", state)

    def test_player_led_bind(self):
        bind = strip_comments(body(COMMANDS, "static bool BindPlayerLed(", "\n}\n"))
        self.assertIn("PL::CheckBind(facts)", bind)
        self.assertIn("sPlayerbotAIConfig.dungeonRunPlayerLed &&", bind)
        self.assertIn("RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::PlayerLed,", bind)
        self.assertNotIn("sPlayerbotAIConfig.remoteCommander", bind)
        self.assertIn("facts.commanderLeads = group->IsLeader(commander->GetObjectGuid());", bind)
        self.assertIn("binding.playerLed = true;", bind)
        self.assertIn("binding.inParty = true;", bind)
        self.assertIn("binding.leadGuid = tank->GetObjectGuid().GetRawValue();", bind)
        # The commander keeps the lead and the bots keep following him: no lead change, no form-up on the tank,
        # no follow target pointed at the tank, nobody leaves or joins the party.
        for absent in ("ChangeLeader", "FormUpOnLead", "PointFollowerAtLead", "RemoveMember", "JoinLeadGroup",
                       "DropCommanderFromParty"):
            self.assertNotIn(absent, bind)
        # Hires stay hires while bound, as with `bind ... inparty`.
        self.assertIn("hires.BeginCommand(", bind)
        self.assertIn("hires.ConfirmCommand(", bind)

    def test_run_start_player_word(self):
        run = strip_comments(body(COMMANDS, "static bool RunVerbAs(", "\n}\n"))
        self.assertIn("TortoiseBots::PlayerLed::TakeStart(speedArgs.rest)", run)
        self.assertIn("BindPlayerLed(handler, commander, playerLedStart.tank, intent)", run)
        self.assertIn("arg = playerLedStart.rest;", run)
        self.assertIn("slot.playerLed.on = playerLedStart.playerLed;", run)
        # A refused start after the bind releases it; a started run keeps it.
        self.assertIn("playerLedGuard.commanderRaw = commanderRaw;", run)
        self.assertIn("playerLedGuard.commanderRaw = 0;", run)
        guard = strip_comments(body(COMMANDS, "struct PlayerLedStartGuard", "\n};\n"))
        self.assertIn("ReleaseBinding(commanderRaw, RemoteCommander::ReleaseReason::Unbound, false);", guard)
        # The player stands in the plan's dungeon.
        self.assertIn("playerLedStart.playerLed && plan->map != commander->GetMapId()", run)
        # `.bot run mode` stays with the dispatcher in HandleDungeonRun (its own binding-free branch).
        dispatch = body(COMMANDS, "bool HandleDungeonRun(", "\n}\n")
        self.assertIn('verb != "mode"', dispatch)
        self.assertIn("TortoiseBots::PlayerLed::ModeText(", run)

    def test_player_led_binding_ends_with_the_run(self):
        run = strip_comments(body(COMMANDS, "static bool RunVerbAs(", "\n}\n"))
        stop = run[run.index('if (verb == "stop")'):]
        stop = stop[:stop.index("return true;")]
        self.assertIn("if (playerLed)", stop)
        self.assertIn("ReleaseBinding(commanderRaw, RemoteCommander::ReleaseReason::Unbound, false);", stop)
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n"))
        self.assertIn("slot.playerLed.on && (!sPlayerbotAIConfig.dungeonRunPlayerLed ||", tick)
        self.assertGreaterEqual(tick.count("ReleaseBinding(commanderRaw, RemoteCommander::ReleaseReason::Unbound, false);") + tick.count("ReleaseBinding(eraseKey, RemoteCommander::ReleaseReason::Unbound, false);"), 2)
        update = strip_comments(body(COMMANDS, "static void UpdateDungeonRuns(uint32 diff)", "\n}\n"))
        self.assertIn("ReleaseBinding(commanderRaw, RemoteCommander::ReleaseReason::Disabled, true);", update)


class PlayerLedOrderWiringTest(unittest.TestCase):
    def test_player_orders_do_not_pause_a_player_led_run(self):
        pause = strip_comments(body(COMMANDS, "void PauseDungeonRunForPlayer(uint64 commanderRaw, std::string const& order)\n{", "\n}\n"))
        led = pause.index("it->second.playerLed.on")
        self.assertLess(led, pause.index("TortoiseBots::DungeonRun::PauseForPlayer("))
        self.assertIn("TortoiseBots::PlayerLed::NotePlayerOrder(it->second.playerLed, it->second.run, ProtocolSafe(order))", pause)
        # The run's own orders never reach it.
        self.assertLess(pause.index("if (s_runIssuing)"), led)

    def test_leading_players_chat_is_an_order(self):
        chat = strip_comments(body(COMMANDS, "bool NoteHumanChatOrder(Player* bot, Player& from, std::string const& text, bool whisper)", "\n}\n"))
        led = chat.index("if (fromCommander && runExists && run->second.playerLed.on)")
        self.assertLess(led, chat.index("RemoteCommander::ClassifyRunBoundOrder("))
        branch = chat[led:chat.index("RemoteCommander::ClassifyRunBoundOrder(")]
        self.assertIn("PauseDungeonRunForPlayer(commanderRaw,", branch)
        self.assertIn("return true;", branch)

    def test_every_order_hook_still_reaches_the_note(self):
        # `.bot action`, `.bot cmd` and the chat hook all go through PauseDungeonRunForPlayer.
        code = strip_comments(COMMANDS)
        self.assertIn('PauseDungeonRunForPlayer(requester->GetObjectGuid().GetRawValue(), "action " + intent);', code)
        self.assertIn('PauseDungeonRunForPlayer(order.commander->GetObjectGuid().GetRawValue(), "cmd " + verb);', code)


class PlayerLedGlueWiringTest(unittest.TestCase):
    def test_tick_picks_before_the_facts(self):
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)\n{", "\n}\n"))
        pick = tick.index("TortoiseBots::PlayerLed::Pick(slot.run, slot.playerLed, anchor, ReadPlayerLedEngaged(commander, bots), pathOk);")
        self.assertLess(pick, tick.index("ReadRunFacts(slot, lead, bots, facts, binding->raid);"))
        self.assertIn("slot.playerLed.pullYd = sPlayerbotAIConfig.dungeonRunPlayerLedPullYd;", tick)
        self.assertIn("anchor = ReadPlayerLedAnchor(commander, bots, slot.run.plan->map, slot.playerLed);", tick)
        self.assertIn("? TortoiseBots::PlayerLed::Tick(slot.run, slot.playerLed, facts, anchor, dt)", tick)
        self.assertIn(": TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);", tick)
        # The tank is never put on stay for a rest in a player-led run.
        self.assertIn("PlayerbotAI* leadAi = slot.playerLed.on ? nullptr : PlayerbotAIStorage::Instance().GetAI(lead)", tick)

    def test_anchor_reads_the_player_only(self):
        read = strip_comments(body(COMMANDS, "TortoiseBots::PlayerLed::Anchor ReadPlayerLedAnchor(", "\n}\n"))
        self.assertIn("TortoiseBots::PlayerLed::MovedSince(pl, a.x, a.y, a.z)", read)
        self.assertIn("a.moving = moved || commander->IsMoving();", read)
        self.assertIn("a.botsMaxYd = yd;", read)
        self.assertIn("bot->GetDistance(commander)", read)
        # Read only: nothing is ordered to the player.
        for absent in ("ExecuteQuietAction", "TeleportTo", "RequestSummon", "SetStrategy", "ChangeStrategy"):
            self.assertNotIn(absent, read)
        engaged = strip_comments(body(COMMANDS, "std::vector<TortoiseBots::PlayerLed::Engaged> ReadPlayerLedEngaged(", "\n}\n"))
        self.assertIn("victim->GetAttackers()", engaged)
        self.assertIn("add(bot->GetPet());", engaged)
        self.assertNotIn("Cell::Visit", engaged)  # no grid search

    def test_pull_anchor_and_summon_point_are_the_player(self):
        anchor = strip_comments(body(COMMANDS, "Player* CommanderPullAnchor(CommanderOrder const& order)", "\n}\n"))
        self.assertIn("if (order.playerLed && commander && commander->IsInWorld() && commander->IsAlive() && order.lead &&", anchor)
        self.assertIn("return order.lead;", anchor)
        order = strip_comments(body(COMMANDS, "bool HandleCommanderOrder(", "\n}\n"))
        self.assertIn("order.playerLed = binding->playerLed;", order)
        self.assertIn("return RunPullOrder(handler, context, context.requester, verb, option, intent);", order)
        context = strip_comments(body(COMMANDS, "BotCommandContext CommanderContext(CommanderOrder const& order, Unit* target)", "\n}\n"))
        self.assertIn("context.requester = CommanderPullAnchor(order);", context)
        self.assertIn("Player* lead = context.requester;", context)
        summon = strip_comments(body(COMMANDS, "static bool CommanderSummon(", "\n}\n"))
        self.assertIn("Player* lead = CommanderPullAnchor(order);", summon)
        # The summon moves the bot to the point, never the player.
        self.assertIn("RequestSummon(order.commander, bot, lead)", summon)

    def test_form_up_is_on_the_player(self):
        form = strip_comments(body(COMMANDS, "uint32 FormUpOnLead(Player* commander, Player* lead, std::vector<Player*> const& bots,", "\n}\n"))
        branch = form.index("if (binding->playerLed)")
        self.assertLess(branch, form.index("PointFollowerAtLead("))
        self.assertIn("return FormUpOnPlayer(commander, bots);", form)
        player = strip_comments(body(COMMANDS, "uint32 FormUpOnPlayer(Player* commander, std::vector<Player*> const& bots)", "\n}\n"))
        self.assertIn('ExecuteQuietAction(ai, "follow chat shortcut", ai::Event("follow", "", commander))', player)
        self.assertNotIn("PointFollowerAtLead", player)
        self.assertNotIn('"stay', player)

    def test_skip_with_nothing_picked(self):
        run = strip_comments(body(COMMANDS, "static bool RunVerbAs(", "\n}\n"))
        self.assertIn("slot.playerLed.on && slot.playerLed.picked == TortoiseBots::PlayerLed::kNone", run)
        self.assertIn("TortoiseBots::PlayerLed::SkipNearest(slot.run, slot.playerLed, here)", run)


class PlayerLedAwayAndPathWiringTest(unittest.TestCase):
    def test_away_key(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn('config.GetIntDefault("AiPlayerbot.DungeonRunPlayerLedAwayMinutes", 10)', keys)
        self.assertIn("AiPlayerbot.DungeonRunPlayerLedAwayMinutes = 10", TEMPLATE)
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)\n{", "\n}\n"))
        self.assertIn("slot.playerLed.awayMinutes = sPlayerbotAIConfig.dungeonRunPlayerLedAwayMinutes;", tick)

    def test_path_check_is_the_tanks_navmesh_path(self):
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)\n{", "\n}\n"))
        check = tick[tick.index("TortoiseBots::PlayerLed::PathCheck const pathOk"):tick.index("TortoiseBots::PlayerLed::Pick(")]
        self.assertIn("PathFinder path(lead);", check)
        self.assertIn("TortoiseBots::PlayerLed::StepSpot(run, step, x, y, z);", check)
        self.assertIn("TortoiseBots::PlayerLed::PathShortEnough(noPath, path.Length(), DistanceTo(lead, x, y, z))", check)
        self.assertIn("(type & PATHFIND_NOPATH)", check)
        self.assertIn("ReadPlayerLedEngaged(commander, bots), pathOk);", tick)

    def test_hold_and_follow_orders_reach_the_party_only(self):
        # The S5 hold is `cmd stay` (every bot: the stay chat shortcut) and `cmd follow` (FormUpOnLead, which a
        # player-led binding turns into FormUpOnPlayer): bots only, never the player.
        order = strip_comments(body(COMMANDS, "static bool CommanderStayFollowRest(", "\n}\n"))
        self.assertIn("succeeded = FormUpOnLead(order.commander, order.lead, order.bots, false);", order)
        self.assertIn("for (Player* bot : order.bots)", order)


class PlayerLedReleaseWiringTest(unittest.TestCase):
    def test_release_tells_the_policy_it_is_player_led(self):
        release = strip_comments(body(COMMANDS, "uint32 ReleaseBinding(uint64 commanderRaw, RemoteCommander::ReleaseReason reason, bool notify)", "\n}\n"))
        facts = release.index("present.playerLed = removed.playerLed;")
        self.assertLess(facts, release.index("RemoteCommander::DecideReleaseParty(present)"))


class PlayerLedRound1WiringTest(unittest.TestCase):
    def test_commanded_bots_are_the_commanders(self):
        fill = strip_comments(body(COMMANDS, "void FillCommandedBots(Group* group, Player* lead, std::vector<Player*>& bots)", "\n}\n"))
        self.assertIn("CommanderBindings().FindByGroup(group->GetId())", fill)
        self.assertIn("TortoiseBots::PlayerLed::CommandsBot(true, owner && record && CanControlBot(owner, record))", fill)
        self.assertIn("IsLiveHeadlessBot(lead) && commanded(lead)", fill)
        self.assertIn("IsLiveHeadlessBot(member) && commanded(member)", fill)

    def test_named_tank_pulls(self):
        order = strip_comments(body(COMMANDS, "bool HandleCommanderOrder(", "\n}\n"))
        sel = order.index("context.selectedBot = order.lead;")
        self.assertLess(order.index("if (order.playerLed)"), sel)
        self.assertLess(sel, order.index("return RunPullOrder(handler, context, context.requester, verb, option, intent);"))

    def test_anchor_names_the_farthest_bot(self):
        read = strip_comments(body(COMMANDS, "TortoiseBots::PlayerLed::Anchor ReadPlayerLedAnchor(", "\n}\n"))
        self.assertIn("a.farthestBot = bot->GetName();", read)


if __name__ == "__main__":
    unittest.main()
