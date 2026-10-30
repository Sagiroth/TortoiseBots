"""The glue of the bot-led run with real players in the party (1 to 4 players, the rest bots).

The policy (runtime/RemoteCommanderPolicy.h, runtime/DungeonRunPolicy.h, tools/test_player_in_party_policy.cpp) decides;
these checks pin that commands/BotCommands.cpp and ai/playerbot/PlayerbotAI.cpp hand it the facts and act on its
answers: the `inparty` bind keeps the commander in the party, the binding's state does not count the commander as a
joined player, a bot leader of a commanded group keeps the lead, a player's chat or `.bot action` is classed (pause,
chat, refusal), the run reads every player of the bound group, and the run's revive, summon and teleport still reach
bots only.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
QUERY = (ROOT / "runtime/CommandedGroupQuery.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PlayerInPartyWiringTest(unittest.TestCase):
    def test_binding_state_does_not_count_the_commander(self):
        state = strip_comments(body(COMMANDS, "RemoteCommander::BindingState ReadBindingState(", "\n}\n"))
        self.assertIn("slot.guid.GetRawValue() != binding.commanderGuid && !BotManager::Instance().IsBot(slot.guid)", state)
        self.assertIn("state.commanderInGroup = group->IsMember(ObjectGuid(binding.commanderGuid));", state)

    def test_release_takes_the_bots_out_when_the_commander_is_gone(self):
        rel = strip_comments(body(COMMANDS, "uint32 ReleaseBinding(uint64 commanderRaw,", "\n}\n"))
        # The bound group is found without the live lead too (round 3): the pure pick over the lead's group, the
        # group store by id and a surviving follower's or the commander's group.
        self.assertIn("Group* const byId = removed.groupId ? sObjectMgr.GetGroupById(removed.groupId) : nullptr;", rel)
        self.assertIn("survivors.push_back(commanderRaw);", rel)
        self.assertIn("groupFacts.leadGroupMatches = leadGroup && leadGroup->GetId() == removed.groupId;", rel)
        self.assertIn("groupFacts.groupById = byId != nullptr;", rel)
        self.assertIn("groupFacts.groupViaMember = viaMember != nullptr;", rel)
        self.assertIn("switch (RemoteCommander::PickReleaseGroup(groupFacts))", rel)
        self.assertIn("case RemoteCommander::ReleaseGroupSource::ById: boundGroup = byId; break;", rel)
        self.assertIn("case RemoteCommander::ReleaseGroupSource::Member: boundGroup = viaMember; break;", rel)
        self.assertIn("not found, nothing to clean up", rel)
        self.assertIn("if (removed.inParty)\n        if (Group* group = boundGroup)", rel)
        self.assertIn("present.leadFound = lead && group->IsMember(lead->GetObjectGuid());", rel)
        self.assertIn("if (group->IsLeader(ObjectGuid(removed.leadGuid)))", rel)
        # The decision is the pure DecideReleaseParty, fed with the reason and what the glue reads of him.
        self.assertIn("present.reason = reason;", rel)
        self.assertIn("present.inWorld = commander && commander->IsInWorld();", rel)
        self.assertIn("commander->GetSession()->HasNetworkTransport();", rel)
        self.assertIn("present.isMember = group->IsMember(ObjectGuid(commanderRaw));", rel)
        self.assertIn("RemoteCommander::DecideReleaseParty(present);", rel)
        self.assertIn("if (action == RemoteCommander::ReleasePartyAction::BotsLeave)", rel)
        # The lead first to an online real player, never the gone commander.
        self.assertIn("if (slot.guid.GetRawValue() == commanderRaw || BotManager::Instance().IsBot(slot.guid))\n"
                      "                                continue;", rel)
        self.assertLess(rel.index("group->ChangeLeader(slot.guid);"), rel.index("group->RemoveMember(guid, 0);"))
        self.assertIn("if (BotManager::Instance().IsBot(slot.guid))\n                            leaving.push_back(slot.guid);", rel)
        self.assertIn("group->RemoveMember(guid, 0);", rel)
        self.assertIn("else if (action == RemoteCommander::ReleasePartyAction::LeadToCommander &&\n"
                      "                    group->IsLeader(ObjectGuid(removed.leadGuid)))\n"
                      "                    group->ChangeLeader(ObjectGuid(commanderRaw));", rel)
        # Before the hires are ended (they are still held by the binding while they leave the party).
        self.assertLess(rel.index("group->RemoveMember(guid, 0);"), rel.index("HireLifecycle::Instance().EndCommand("))

    def test_inparty_bind_keeps_the_commander(self):
        bind = strip_comments(body(COMMANDS, "static bool HandleCommanderBind(", "\nbool HandleCommander("))
        self.assertIn("bool const inParty = RemoteCommander::TakeBindInParty(names);", bind)
        self.assertIn("facts.inParty = inParty;", bind)
        self.assertIn("facts.partyPlayers", bind)
        self.assertIn("facts.commanderLeadsParty = commanderParty && commanderParty->IsLeader(commander->GetObjectGuid());", bind)
        # The gate runs before the lead changes hands.
        self.assertLess(bind.index("RemoteCommander::CheckBind(facts)"), bind.index("group->ChangeLeader(lead->GetObjectGuid())"))
        self.assertIn("binding.inParty = inParty;", bind)
        # The commander is never dropped from an in-party binding's party.
        self.assertIn("if (keepParty && !inParty && lead->GetGroup() && !DropCommanderFromParty(lead->GetGroup(), commander))",
                      bind)
        full = bind.index("lead->GetGroup()->IsFull())")
        self.assertLess(bind.index("if (inParty)\n                return failBind(\"party-full\"", full),
                        bind.index("if (!DropCommanderFromParty(lead->GetGroup(), commander))", full))
        # Without the word the plan is today's; with it the commander's party is kept whoever plays in it.
        self.assertIn("RemoteCommander::BindGroupPlan const plan = inParty\n"
                      "        ? (planFacts.leadInCommanderParty ? RemoteCommander::BindGroupPlan::KeepCommanderParty", bind)
        self.assertIn(": RemoteCommander::DecideBindGroup(planFacts);", bind)
        # Other real players stay members, never bots to command; unnamed BOTS still refuse the bind.
        self.assertIn("if (inParty && !BotManager::Instance().IsBot(slot.guid))\n                continue;", bind)
        self.assertIn("BotManager::Instance().IsBot(slot.guid) &&\n"
                      "                    std::find(named.begin(), named.end(), slot.guid) == named.end())", bind)

    def test_commanded_bot_keeps_its_master_and_lead(self):
        code = strip_comments(AI)
        self.assertIn("bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(bot);", code)
        self.assertIn("if (group->IsLeader(bot->GetObjectGuid()) && !commanded)", code)
        self.assertIn("if (!dungeonCrew && !commanded && master && master != bot && !HasActivePlayerMaster()", code)
        self.assertIn("!(commanded && IsRealPlayer(newMaster)) &&", code)

    def test_chat_order_is_classed(self):
        code = strip_comments(AI)
        self.assertIn("if (!TortoiseBots::BotCommands::NoteHumanChatOrder(bot, fromPlayer, filtered, "
                      "type == CHAT_MSG_WHISPER))", code)
        self.assertIn("bool NoteHumanChatOrder(Player* bot, Player& from, std::string const& text, bool whisper);", QUERY)
        note = strip_comments(body(COMMANDS, "bool NoteHumanChatOrder(Player* bot, Player& from,", "\n}\n"))
        self.assertIn("RemoteCommander::ClassifyRunBoundOrder(", note)
        self.assertIn("case RemoteCommander::RunOrderVerdict::PauseRun:", note)
        self.assertIn("case RemoteCommander::RunOrderVerdict::Refuse:", note)
        self.assertIn("PauseDungeonRunForPlayer(commanderRaw,", note)
        self.assertIn("RemoteCommander::RunBoundRefusalText(", note)

    def test_bot_action_of_another_player_is_refused(self):
        action = strip_comments(body(COMMANDS, "bool HandleAction(ChatHandler* handler, char const* args)", "\n}\n"))
        self.assertIn("    if (parsed)\n        if (uint64 const owner = BoundForeignCommander(requester))", action)
        refuse = action.index("BoundForeignCommander(requester)")
        self.assertLess(refuse, action.index("\"run-bound\""))
        self.assertLess(action.index("\"run-bound\""), action.index("PauseDungeonRunForPlayer("))
        # From the bind on, a run or not.
        foreign = strip_comments(body(COMMANDS, "uint64 BoundForeignCommander(Player* player)\n{", "\n}\n"))
        self.assertNotIn("DungeonRuns()", foreign)
        note = strip_comments(body(COMMANDS, "bool NoteHumanChatOrder(Player* bot, Player& from,", "\n}\n"))
        self.assertNotIn("if (run == DungeonRuns().end())\n            continue;", note)
        self.assertIn("ClassifyRunBoundOrder(true, runExists, running, fromCommander, whisper)", note)
        # The commander's own order still pauses.
        self.assertIn("PauseDungeonRunForPlayer(requester->GetObjectGuid().GetRawValue(), \"action \" + intent);", action)

    def test_run_reads_every_player(self):
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n"))
        read = tick.index("ReadRunFacts(slot, lead, bots, facts, binding->raid);")
        players = tick.index("ReadRunPlayers(slot, commanderRaw, lead, group, bots, facts);")
        health = tick.index("TortoiseBots::DungeonRun::ApplyPlayerFacts(facts);")
        order = tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt)")
        self.assertTrue(read < players < health < order)
        fn = strip_comments(body(COMMANDS, "void ReadRunPlayers(DungeonRunSlot const& slot,", "\n}\n"))
        self.assertIn("if (BotManager::Instance().IsBot(entry.guid))\n            continue;", fn)
        self.assertIn("p.commander = entry.guid.GetRawValue() == commanderRaw;", fn)
        self.assertIn("p.onMap = member->IsInWorld() && member->GetMapId() == slot.run.plan->map", fn)
        self.assertIn("p.ghost = member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)", fn)
        self.assertIn("f.players.push_back(p);", fn)
        self.assertIn("f.itemPlayer", fn)
        self.assertIn("f.lootTurnPlayer", fn)
        self.assertIn("p.online = false;", fn)
        self.assertIn("member->IsInCombat()", fn)
        self.assertIn("p.gm = IsBotAdministrator(member);", fn)
        self.assertIn("if (p.inCombat)\n            fighting.push_back(member);", fn)
        self.assertIn("ReadEngagedUnits(lead, fighting, f);", fn)
        self.assertIn("bool const raidBoss = TortoiseBots::DungeonRun::RaidBossRulesFight(slot.run, f);", fn)
        self.assertIn("if (fresh)\n        ReadGuardFacts(lead, bots, f, hold, raidBoss);", fn)
        self.assertIn("AddPlayerAttackers(lead, bots, fighting, f, hold, raidBoss);", fn)
        add = strip_comments(body(COMMANDS, "void AddPlayerAttackers(Player* lead,", "\n}\n"))
        # A raid boss fight reads a player's attackers as ReadGuardFacts reads a bot's: around the same anchor, within
        # kRaidBossGuardYd, with the victim's name; else around the lead bot within kGuardYd, as before.
        self.assertIn("Player* const anchor = raidBoss ? RaidGuardAnchor(lead, bots, f) : lead;", add)
        self.assertIn("player->GetDistance(anchor) > range", add)
        self.assertIn("fact.distLead = anchor->GetDistance(attacker);", add)
        self.assertIn("fact.attackable = GroupCanAttack(anchor, attacker);", add)
        self.assertIn("fact.victimName = player->GetName();", add)
        self.assertIn("attacker->GetVictim() != player", add)
        self.assertIn("f.partyAttackers.push_back(fact);", add)

    def test_revive_summon_and_teleport_reach_bots_only(self):
        fill = strip_comments(body(COMMANDS, "void FillCommandedBots(Group* group, Player* lead,", "\n}\n"))
        self.assertIn("IsLiveHeadlessBot(member)", fill)
        self.assertIn("order.bots = CommandedBots(order.group, order.lead);", COMMANDS)
        revive = strip_comments(body(COMMANDS, "if (word == \"party\")", "SendActionAck(handler, intent, \"party\", revived"))
        self.assertIn("for (Player* member : order.bots)", revive)
        teleport = strip_comments(body(COMMANDS, "bool RunStrandedTeleport(Player* commander, std::string const& order, std::string& answer)\n{", "\n}\n"))
        self.assertEqual(teleport.count("NearTeleportTo("), 1)
        self.assertIn("lead->NearTeleportTo(", teleport)


if __name__ == "__main__":
    unittest.main()
