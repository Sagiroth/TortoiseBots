"""The run's teleport return and stand-still hold are wired through the glue and the bot.

The policy (runtime/DungeonRunPolicy.h FarReturnTick, tools/test_teleport_return_policy.cpp) sends
`fetch x y z <names>`, reads the summons the portal dropped, and lists the bots it is bringing back
(Run::returnHold). This checks the glue (commands/Commander.cpp) dispatches the order to a teleport
that drops combat first, fills the facts the policy reads, stamps the hold every tick whatever the
run's state, that the summon portal counts what it drops (behavior/PlayerConvenience.cpp), and that
the bot's CanMove reads the hold (ai/playerbot/PlayerbotAI.cpp).
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
CONVENIENCE = (ROOT / "behavior/PlayerConvenience.cpp").read_text()
AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TeleportReturnWiringTest(unittest.TestCase):
    def test_fetch_order_dispatches_to_the_combat_dropping_teleport(self):
        issue = body(COMMANDS, "bool IssueRunOrder(", "HandleCommanderOrder(handler.get()")
        self.assertIn('if (text.compare(0, 6, "fetch ") == 0)\n        return RunFetchReturn(commander, text, answer);',
                      issue)
        fetch = body(COMMANDS, "bool RunFetchReturn(Player* commander, std::string const& order, std::string& answer)\n{", "\n}\n")
        self.assertIn("BotManager::Instance().IsBot(slot.guid)", fetch)  # bot-only parties
        self.assertIn("PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid())", fetch)
        # Combat drops only after the teleport went: a refused one leaves the fight untouched.
        drop = fetch.index("sServerFacade.GetHostileRefManager(bot).deleteReferences();")
        stop = fetch.index("bot->CombatStop();")
        tele = fetch.index("bot->TeleportTo(lead->GetMapId(), x, y, z")
        refused = fetch.index("(the teleport was refused)")
        self.assertLess(tele, refused)
        self.assertLess(refused, drop)
        self.assertLess(refused, stop)
        self.assertIn("bot->ExecuteTeleportNear();", fetch)
        self.assertIn('"fetch %f %f %f%n"', fetch)
        # The policy's refusal rule matches the same prefix.
        refused = body(POLICY, "inline void OrderRefused(Run& r", "\n}\n")
        self.assertIn('order.compare(0, 6, "fetch ") == 0', refused)

    def test_facts_carry_the_keys_the_cancels_and_the_ground_read(self):
        facts = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot", "\n}\n")
        self.assertIn("f.teleportReturnYd = sPlayerbotAIConfig.runTeleportReturnYd;", facts)
        self.assertIn("f.reviveHoldYd = sPlayerbotAIConfig.dungeonReviveHoldYd;", facts)
        self.assertIn("if (slot.run.fetchReadWanted && lead->IsAlive() && lead->IsInWorld())\n        ReadFetchFacts(lead, bots, f);",
                      facts)
        self.assertIn("member.summonCancels = PlayerConvenience::Instance().SummonCancels(bot->GetObjectGuid());", facts)
        read = body(COMMANDS, "static void ReadFetchFacts(", "\n}\n")
        self.assertIn("c->IsInCombat()", read)  # only packs not pulled yet
        self.assertIn("f.fetchReach.push_back(reach);", read)
        self.assertIn("f.fetchSpots.push_back(point);", read)
        self.assertIn("f.fetchRead = true;", read)

    def test_the_portal_counts_what_it_drops(self):
        update = body(CONVENIENCE, "void PlayerConvenience::UpdateSummons(", "\n}\n")
        for line in ("Summon cancelled bot %s became incompatible before teleport",
                     "Summon aborted bot %s still in combat",
                     "Summon teleport rejected for bot %s"):
            after = update[update.index(line):]
            self.assertIn("++m_summonCancels[state.botGuid.GetCounter()];", after[:300], line)
        self.assertNotIn("++m_summonCancels", body(update, "became incompatible before follow restore", "continue;"))

    def test_hold_stamped_every_tick_before_the_state_checks(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        sync = tick.index("SyncReturnHolds(commander, *binding, slot, dt);")
        self.assertLess(sync, tick.index("if (slot.run.state != TortoiseBots::DungeonRun::State::Running || !commander)"))
        self.assertLess(sync, tick.index("RunTimeLimitReached"))
        hold = body(COMMANDS, "static void SyncReturnHolds(", "\nvoid TickDungeonRun")
        self.assertIn("slot.run.returnHold.count(name)", hold)
        self.assertIn("PlayerConvenience::Instance().IsBusy(bot->GetObjectGuid())", hold)
        self.assertIn("ai::ShouldArmReviveHold(holdYd, bot->GetMap()->IsDungeon(), yd)", hold)
        self.assertIn('GetValue<int32>("manual int", "return hold until ms")->Set(', hold)
        self.assertIn("(now + ai::RETURN_HOLD_MARK_MS - 1) | 1", hold)

    def test_can_move_reads_the_hold(self):
        can = body(AI, "bool PlayerbotAI::CanMove(bool ignoreReturnHold)", "\n}\n")
        # The mark is only written with the key on, so the lookup itself waits for it.
        self.assertIn("if (!ignoreReturnHold && sPlayerbotAIConfig.dungeonReviveHoldYd > 0.0f && ReturnHeld())\n"
                      "    {\n        return false;", can)
        held = body(AI, "bool PlayerbotAI::ReturnHeld()", "\n}\n")
        self.assertIn('"return hold until ms"', held)
        self.assertIn("ai::ReturnHoldOn(", held)
        # The force-stop of a bot that may not move covers a follow already under way.
        self.assertIn("if (!bot->IsStopped() && !IsJumping() && !CanMove()", AI)

    def test_policy_tick_order(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{", "\n}\n")
        self.assertLess(tick.index("UpdateSummonOutcomes(r, f);"), tick.index("bool const runBackWait = RunBack(r, f, dt);"))
        self.assertLess(tick.index("bool const runBackWait = RunBack(r, f, dt);"),
                        tick.index("if (FarReturnTick(r, f, dt, order))"))
        # Before the regroup's out-of-combat block, so a fighting runner is still brought back.
        self.assertLess(tick.index("if (FarReturnTick(r, f, dt, order))"),
                        tick.index("if (r.regroup && !runBackWait && !f.anyInCombat)"))

    def test_keys_and_template_agree(self):
        self.assertIn('config.GetFloatDefault("AiPlayerbot.RunTeleportReturnYd", 0.0f)', CONFIG)
        self.assertIn('config.GetFloatDefault("AiPlayerbot.DungeonReviveHoldYd", 0.0f)', CONFIG)
        self.assertRegex(TEMPLATE, re.compile(r"^AiPlayerbot\.RunTeleportReturnYd = 0$", re.M))
        self.assertRegex(TEMPLATE, re.compile(r"^AiPlayerbot\.DungeonReviveHoldYd = 0$", re.M))
        self.assertRegex(TEMPLATE, re.compile(r"^AiPlayerbot\.DungeonFollowHoldBeforePacks = 0$", re.M))


if __name__ == "__main__":
    unittest.main()
